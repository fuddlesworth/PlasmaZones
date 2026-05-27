// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSettingsUi/ApplicationController.h"

#include <QDebug>

#include "PhosphorSettingsUi/PageController.h"
#include "PhosphorSettingsUi/PageRegistry.h"
#include "PhosphorSettingsUi/StagingDomain.h"

namespace PhosphorSettingsUi {

ApplicationController::ApplicationController(QObject* parent)
    : QObject(parent)
    , m_registry(new PageRegistry(this))
{
}

ApplicationController::~ApplicationController() = default;

PageRegistry* ApplicationController::registry() const
{
    return m_registry;
}

bool ApplicationController::isDirty() const
{
    return m_dirty;
}

QString ApplicationController::currentPageId() const
{
    return m_currentPageId;
}

void ApplicationController::setCurrentPageId(const QString& id)
{
    if (m_currentPageId == id) {
        return;
    }
    if (!id.isEmpty() && !m_registry->hasPage(id)) {
        qWarning() << "ApplicationController::setCurrentPageId: unknown page" << id;
        return;
    }
    m_currentPageId = id;
    Q_EMIT currentPageIdChanged();
}

void ApplicationController::registerPage(PageController* page, const QString& parentId, const QString& title,
                                         const QUrl& qmlSource, const QString& iconSource, bool isCollapsible,
                                         bool hasDividerAfter)
{
    if (!page) {
        qWarning() << "ApplicationController::registerPage: null page";
        return;
    }
    if (!page->parent()) {
        page->setParent(this);
    }

    PageRegistry::Entry entry;
    entry.id = page->id();
    entry.parentId = parentId;
    entry.title = title;
    entry.iconSource = iconSource;
    entry.qmlSource = qmlSource;
    entry.controller = page;
    entry.isCollapsible = isCollapsible;
    entry.hasDividerAfter = hasDividerAfter;
    m_registry->registerPage(std::move(entry));

    trackDomain(page);
}

void ApplicationController::registerDomain(StagingDomain* domain)
{
    if (!domain) {
        qWarning() << "ApplicationController::registerDomain: null domain";
        return;
    }
    if (!domain->parent()) {
        domain->setParent(this);
    }
    trackDomain(domain);
}

void ApplicationController::applyAll()
{
    for (const auto& domain : m_domains) {
        if (domain && domain->isDirty()) {
            domain->apply();
        }
    }
    recomputeDirty();
}

void ApplicationController::discardAll()
{
    for (const auto& domain : m_domains) {
        if (domain && domain->isDirty()) {
            domain->discard();
        }
    }
    recomputeDirty();
}

void ApplicationController::resetCurrentPage()
{
    if (m_currentPageId.isEmpty()) {
        return;
    }
    if (auto* page = m_registry->controller(m_currentPageId)) {
        page->resetToDefaults();
    }
}

namespace {
// Collects the registry's in-order list of navigable (qmlSource set)
// page ids and locates the current page's index inside it. Returns
// `currentIdx = -1` when the current page id is empty / not in the
// navigable set — the caller decides how to wrap.
struct NavigableState
{
    QStringList ids;
    int currentIdx = -1;
};

NavigableState collectNavigable(const PageRegistry* registry, const QString& currentPageId)
{
    NavigableState out;
    for (const auto& e : registry->allPages()) {
        if (e.qmlSource.isEmpty()) {
            continue;
        }
        if (e.id == currentPageId) {
            out.currentIdx = out.ids.size();
        }
        out.ids.append(e.id);
    }
    return out;
}
} // namespace

QString ApplicationController::gotoPreviousPage()
{
    const auto state = collectNavigable(m_registry, m_currentPageId);
    if (state.ids.isEmpty()) {
        return QString();
    }
    const int next = state.currentIdx <= 0 ? state.ids.size() - 1 : state.currentIdx - 1;
    setCurrentPageId(state.ids.at(next));
    return state.ids.at(next);
}

QString ApplicationController::gotoNextPage()
{
    const auto state = collectNavigable(m_registry, m_currentPageId);
    if (state.ids.isEmpty()) {
        return QString();
    }
    const int next = state.currentIdx < 0 || state.currentIdx == state.ids.size() - 1 ? 0 : state.currentIdx + 1;
    setCurrentPageId(state.ids.at(next));
    return state.ids.at(next);
}

QStringList ApplicationController::parentChainFor(const QString& id) const
{
    // Cycle guard for parent-id graph walks. 32 hops is well above any
    // realistic sidebar nesting (typical settings apps cap at 3-4 levels)
    // and well below the cost of any pathological cycle.
    constexpr int kMaxParentChainHops = 32;

    QStringList chain;
    QString cursor = id;
    // Walk parent links upward; cap at kMaxParentChainHops as a cycle guard.
    for (int i = 0; i < kMaxParentChainHops; ++i) {
        if (!m_registry->hasPage(cursor)) {
            // Unknown id is a legitimate query path (QML probing during
            // bootstrap before pages are registered). Return whatever
            // we've collected so far without warning — the warning below
            // is reserved for actual cycle / depth-exceeded cases.
            return chain;
        }
        const QString parent = m_registry->entry(cursor).parentId;
        if (parent.isEmpty()) {
            return chain;
        }
        chain.prepend(parent);
        cursor = parent;
    }
    // Reached the hop cap on a known starting id without hitting a root —
    // indicates a cycle in the registry's parentId graph (programmer
    // error). Warn so the bug surfaces instead of silently producing a
    // truncated chain.
    qWarning() << "ApplicationController::parentChainFor: cycle or depth>" << kMaxParentChainHops
               << "in registry for id" << id;
    return chain;
}

void ApplicationController::trackDomain(StagingDomain* domain)
{
    const QPointer<StagingDomain> tracked(domain);
    if (m_domains.contains(tracked)) {
        // Same domain registered twice (e.g. once via registerPage and
        // once via registerDomain). Connecting `dirtyChanged` a second
        // time would double-fire `recomputeDirty()` per emit — silently
        // wastes work and is always a caller bug.
        qWarning() << "ApplicationController::trackDomain: domain already tracked, ignoring";
        return;
    }
    m_domains.append(tracked);
    // Qt::UniqueConnection guards against the (already-checked-above) duplicate
    // registration path picking up a stale entry whose QPointer outlived its
    // pointee — defence-in-depth so double-tracking can never double-fire.
    connect(domain, &StagingDomain::dirtyChanged, this, &ApplicationController::onDomainDirtyChanged,
            Qt::UniqueConnection);
    if (domain->isDirty()) {
        recomputeDirty();
    }
}

void ApplicationController::onDomainDirtyChanged()
{
    recomputeDirty();
}

void ApplicationController::recomputeDirty()
{
    bool any = false;
    for (const auto& domain : m_domains) {
        if (domain && domain->isDirty()) {
            any = true;
            break;
        }
    }
    if (any == m_dirty) {
        return;
    }
    m_dirty = any;
    Q_EMIT dirtyChanged();
}

} // namespace PhosphorSettingsUi
