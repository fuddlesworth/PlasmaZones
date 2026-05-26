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
                                         const QUrl& qmlSource, const QString& iconSource, bool isCollapsible)
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

QString ApplicationController::gotoPreviousPage()
{
    const auto entries = m_registry->allPages();
    QStringList navigable;
    int currentIdx = -1;
    for (const auto& e : entries) {
        if (e.qmlSource.isEmpty()) {
            continue;
        }
        if (e.id == m_currentPageId) {
            currentIdx = navigable.size();
        }
        navigable.append(e.id);
    }
    if (navigable.isEmpty()) {
        return QString();
    }
    const int next = currentIdx < 0 ? navigable.size() - 1 : (currentIdx == 0 ? navigable.size() - 1 : currentIdx - 1);
    setCurrentPageId(navigable.at(next));
    return navigable.at(next);
}

QString ApplicationController::gotoNextPage()
{
    const auto entries = m_registry->allPages();
    QStringList navigable;
    int currentIdx = -1;
    for (const auto& e : entries) {
        if (e.qmlSource.isEmpty()) {
            continue;
        }
        if (e.id == m_currentPageId) {
            currentIdx = navigable.size();
        }
        navigable.append(e.id);
    }
    if (navigable.isEmpty()) {
        return QString();
    }
    const int next = currentIdx < 0 ? 0 : (currentIdx == navigable.size() - 1 ? 0 : currentIdx + 1);
    setCurrentPageId(navigable.at(next));
    return navigable.at(next);
}

QStringList ApplicationController::parentChainFor(const QString& id) const
{
    QStringList chain;
    QString cursor = id;
    // Walk parent links upward; cap at 32 hops as a cycle guard.
    for (int i = 0; i < 32; ++i) {
        if (!m_registry->hasPage(cursor)) {
            break;
        }
        const QString parent = m_registry->entry(cursor).parentId;
        if (parent.isEmpty()) {
            break;
        }
        chain.prepend(parent);
        cursor = parent;
    }
    return chain;
}

void ApplicationController::trackDomain(StagingDomain* domain)
{
    m_domains.append(QPointer<StagingDomain>(domain));
    connect(domain, &StagingDomain::dirtyChanged, this, &ApplicationController::onDomainDirtyChanged);
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
