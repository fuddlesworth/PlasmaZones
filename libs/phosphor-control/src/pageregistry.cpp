// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/PageRegistry.h"

#include "PhosphorControl/PageController.h"

#include <QDebug>

namespace PhosphorControl {

PageRegistry::PageRegistry(QObject* parent)
    : QObject(parent)
{
}

PageRegistry::~PageRegistry() = default;

bool PageRegistry::registerPage(Entry entry)
{
    if (entry.id.isEmpty()) {
        qWarning() << "PageRegistry::registerPage: refusing to register page with empty id";
        return false;
    }
    if (m_indexById.contains(entry.id)) {
        qWarning() << "PageRegistry::registerPage: duplicate page id" << entry.id << "— ignoring registration";
        return false;
    }
    if (!entry.parentId.isEmpty() && !m_indexById.contains(entry.parentId)) {
        qWarning() << "PageRegistry::registerPage: page" << entry.id << "references unknown parent" << entry.parentId
                   << "— ignoring registration";
        return false;
    }
    // ApplicationController::resetCurrentPage() and any consumer of
    // PageRegistry::controller(id) will deref the controller pointer;
    // reject null at registration so the failure surfaces here rather
    // than at first use.
    if (!entry.controller) {
        qWarning() << "PageRegistry::registerPage: page" << entry.id << "has null controller — ignoring registration";
        return false;
    }
    // Detect the same controller being registered under two different ids.
    // Reusing the controller would give it two sidebar rows that share one
    // dirty bit (only the first registered id ends up in
    // ApplicationController::m_domains), which the UI cannot render
    // coherently. Always a caller bug. O(1) via m_controllerSet so
    // registration stays linear in K (vs. the prior O(K²) walk).
    PageController* const ctrl = entry.controller.data();
    if (m_controllerSet.contains(ctrl)) {
        qWarning() << "PageRegistry::registerPage: controller already registered — refusing duplicate registration "
                      "under id"
                   << entry.id;
        return false;
    }

    const QString id = entry.id;
    m_indexById.insert(id, m_pages.size());
    m_controllerSet.insert(ctrl);
    m_pages.append(std::move(entry));
    Q_EMIT pageRegistered(id);
    return true;
}

void PageRegistry::setPageVisibility(const QString& id, PageVisibility visibility)
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        qWarning() << "PageRegistry::setPageVisibility: unknown page id" << id << "— ignoring";
        return;
    }
    m_pages[it.value()].visibility = visibility;
}

bool PageRegistry::showAdvanced() const
{
    return m_showAdvanced;
}

void PageRegistry::setShowAdvanced(bool showAdvanced)
{
    if (m_showAdvanced == showAdvanced) {
        return;
    }
    m_showAdvanced = showAdvanced;
    Q_EMIT showAdvancedChanged();
}

bool PageRegistry::modeAllows(PageVisibility v) const
{
    switch (v) {
    case PageVisibility::Always:
        return true;
    case PageVisibility::AdvancedOnly:
        return m_showAdvanced;
    case PageVisibility::SimpleOnly:
        return !m_showAdvanced;
    }
    return true;
}

bool PageRegistry::isEntryVisible(const Entry& entry) const
{
    if (!modeAllows(entry.visibility)) {
        return false;
    }
    // A navigable page (has its own QML) stands on its own tier. A virtual
    // node (category header / drill parent with no QML) is worth showing
    // only if it still leads somewhere — hide it once every descendant has
    // been filtered out.
    if (!entry.qmlSource.isEmpty()) {
        return true;
    }
    for (const Entry& child : m_pages) {
        if (child.parentId == entry.id && isEntryVisible(child)) {
            return true;
        }
    }
    return false;
}

bool PageRegistry::hasPage(const QString& id) const
{
    return m_indexById.contains(id);
}

bool PageRegistry::pageAllowedInCurrentMode(const QString& id) const
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        // Tier filter only — unknown ids are not this method's concern
        // (hasPage() is the existence check), so express no opinion.
        return true;
    }
    return modeAllows(m_pages.at(it.value()).visibility);
}

QString PageRegistry::firstVisibleLeafId(const QString& parentId) const
{
    for (const Entry& e : m_pages) {
        if (e.parentId != parentId || !isEntryVisible(e)) {
            continue;
        }
        if (!e.qmlSource.isEmpty()) {
            return e.id;
        }
        const QString leaf = firstVisibleLeafId(e.id);
        if (!leaf.isEmpty()) {
            return leaf;
        }
    }
    return {};
}

PageController* PageRegistry::controller(const QString& id) const
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        return nullptr;
    }
    // QPointer::data() returns nullptr if the controller was destroyed
    // out-of-order — callers that deref the result must null-check.
    return m_pages.at(it.value()).controller.data();
}

PageRegistry::Entry PageRegistry::entry(const QString& id) const
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        return {};
    }
    return m_pages.at(it.value());
}

QList<PageRegistry::Entry> PageRegistry::topLevelPages() const
{
    QList<Entry> out;
    // Worst-case sizing: every registered page is top-level. Cheap upper
    // bound matches the pattern used by allPagesData() so the variant
    // and entry forms have the same allocation profile.
    out.reserve(m_pages.size());
    for (const Entry& e : m_pages) {
        if (e.parentId.isEmpty()) {
            out.append(e);
        }
    }
    return out;
}

QList<PageRegistry::Entry> PageRegistry::childPages(const QString& parentId) const
{
    QList<Entry> out;
    // Worst-case sizing: every registered page is a child of parentId.
    // Same upper-bound rationale as topLevelPages above.
    out.reserve(m_pages.size());
    for (const Entry& e : m_pages) {
        if (e.parentId == parentId) {
            out.append(e);
        }
    }
    return out;
}

QList<PageRegistry::Entry> PageRegistry::allPages() const
{
    return m_pages;
}

namespace {
// QVariantMap key names shipped to QML via topLevelPagesData / childPagesData
// / pageData. Centralised so QML consumers (Sidebar.qml, Breadcrumbs.qml)
// and a future test/typed-binding generator can reference the same canonical
// strings without typos surviving until runtime.
//
// `inline` is omitted because anonymous-namespace symbols already have
// internal linkage; `constexpr` alone is sufficient.
namespace EntryKeys {
constexpr QLatin1String Id{"id"};
constexpr QLatin1String ParentId{"parentId"};
constexpr QLatin1String Title{"title"};
constexpr QLatin1String IconSource{"iconSource"};
constexpr QLatin1String QmlSource{"qmlSource"};
constexpr QLatin1String IsCollapsible{"isCollapsible"};
constexpr QLatin1String HasDividerAfter{"hasDividerAfter"};
constexpr QLatin1String HasQmlSource{"hasQmlSource"};
} // namespace EntryKeys

QVariantMap entryToVariant(const PageRegistry::Entry& e)
{
    QVariantMap m;
    m.insert(EntryKeys::Id, e.id);
    m.insert(EntryKeys::ParentId, e.parentId);
    m.insert(EntryKeys::Title, e.title);
    m.insert(EntryKeys::IconSource, e.iconSource);
    m.insert(EntryKeys::QmlSource, e.qmlSource);
    m.insert(EntryKeys::IsCollapsible, e.isCollapsible);
    m.insert(EntryKeys::HasDividerAfter, e.hasDividerAfter);
    m.insert(EntryKeys::HasQmlSource, !e.qmlSource.isEmpty());
    return m;
}
} // namespace

QVariantList PageRegistry::topLevelPagesData() const
{
    QVariantList out;
    for (const Entry& e : m_pages) {
        if (e.parentId.isEmpty() && isEntryVisible(e)) {
            out.append(entryToVariant(e));
        }
    }
    return out;
}

QVariantList PageRegistry::childPagesData(const QString& parentId) const
{
    QVariantList out;
    for (const Entry& e : m_pages) {
        if (e.parentId == parentId && isEntryVisible(e)) {
            out.append(entryToVariant(e));
        }
    }
    return out;
}

QVariantMap PageRegistry::pageData(const QString& id) const
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        return {};
    }
    return entryToVariant(m_pages.at(it.value()));
}

QVariantList PageRegistry::allPagesData() const
{
    QVariantList out;
    out.reserve(m_pages.size());
    for (const Entry& e : m_pages) {
        out.append(entryToVariant(e));
    }
    return out;
}

} // namespace PhosphorControl
