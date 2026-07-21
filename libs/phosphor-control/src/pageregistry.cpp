// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/PageRegistry.h"

#include "PhosphorControl/PageController.h"

#include <QDebug>

#include <algorithm>

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
        // The set stores bare addresses and has no removal path, so a hit may
        // be a GHOST: a controller that has since been destroyed, whose address
        // the allocator handed back for this new one. Every Entry guards its
        // pointer with QPointer, so confirm against a live entry before
        // refusing — otherwise a legitimate registration is rejected with a
        // misleading "already registered". Only the error path pays the scan.
        const bool liveDuplicate = std::any_of(m_pages.cbegin(), m_pages.cend(), [ctrl](const Entry& e) {
            return e.controller.data() == ctrl;
        });
        if (liveDuplicate) {
            qWarning() << "PageRegistry::registerPage: controller already registered — refusing duplicate registration "
                          "under id"
                       << entry.id;
            return false;
        }
    }

    const QString id = entry.id;
    const QString parentId = entry.parentId;
    const qsizetype index = m_pages.size();
    m_indexById.insert(id, index);
    m_controllerSet.insert(ctrl);
    // Registration order within a parent is the order the sidebar renders, so
    // appending here preserves it for every accessor that reads the index.
    m_childrenByParent[parentId].append(index);
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
    if (m_pages[it.value()].visibility == visibility) {
        return;
    }
    m_pages[it.value()].visibility = visibility;
    // Announce the restamp: consumers that FILTER on the tier (the sidebar's
    // tree accessors, SearchController's cached index) have no other way to
    // learn an entry changed tier. Deliberately NOT showAdvancedChanged —
    // that is the showAdvanced Q_PROPERTY's NOTIFY, and the mode has not
    // changed here. Firing it would re-evaluate every binding on the property
    // and lie to any consumer that reads it as "the user switched modes".
    Q_EMIT visibleSetChanged();
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
    // A mode flip also changes the visible set, so tier-filtering caches
    // rebuild off the same signal they use for a per-entry restamp.
    Q_EMIT visibleSetChanged();
}

bool PageRegistry::modeAllowsIn(PageVisibility v, bool advanced)
{
    switch (v) {
    case PageVisibility::Always:
        return true;
    case PageVisibility::AdvancedOnly:
        return advanced;
    case PageVisibility::SimpleOnly:
        return !advanced;
    }
    return true;
}

bool PageRegistry::modeAllows(PageVisibility v) const
{
    return modeAllowsIn(v, m_showAdvanced);
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
    const auto it = m_childrenByParent.constFind(entry.id);
    if (it == m_childrenByParent.constEnd()) {
        return false;
    }
    for (const qsizetype idx : it.value()) {
        if (isEntryVisible(m_pages.at(idx))) {
            return true;
        }
    }
    return false;
}

bool PageRegistry::hasPage(const QString& id) const
{
    return m_indexById.contains(id);
}

bool PageRegistry::allowedInMode(const QString& id, bool advanced) const
{
    auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        // Tier filter only — unknown ids are not this method's concern
        // (hasPage() is the existence check), so express no opinion.
        return true;
    }
    // Walk the parent chain, not just this entry's own tier. A page whose own
    // tier passes but whose ancestor category is filtered out has no sidebar
    // row and no drill path to it: isEntryVisible() already hides the ancestor,
    // which takes the whole subtree with it. Answering "yes" here would let the
    // search index, keyboard next/prev, the history skip predicate and the
    // app's mode gate all send the user to a page they cannot navigate back
    // from. Checking only the own tier made those four disagree with the rail.
    //
    // registerPage() rejects an entry whose parent is not already registered,
    // so the parent graph is a DAG and this terminates; the hop cap is a
    // belt-and-braces guard matching parentChainFor's.
    int hops = 0;
    while (it != m_indexById.constEnd() && hops < MaxParentChainHops) {
        const Entry& e = m_pages.at(it.value());
        if (!modeAllowsIn(e.visibility, advanced)) {
            return false;
        }
        if (e.parentId.isEmpty()) {
            return true;
        }
        it = m_indexById.constFind(e.parentId);
        ++hops;
    }
    // Fell out without reaching a root: either the chain is nested deeper than
    // the cap, or a parentId resolved to nothing (structurally impossible —
    // registerPage rejects an unknown parent). Fail CLOSED. Answering "visible"
    // for a page whose ancestry could not be verified is the same lie the
    // ancestor walk above exists to prevent: it would let search, keyboard
    // next/prev, the history skip predicate and the mode gate offer a row the
    // rail cannot draw.
    //
    // Warn once per id: this is a startup-time structural error, but the method
    // runs once per entry per search-index rebuild, once per entry per
    // keyboard next/prev, and once per candidate per history step, so an
    // unguarded warning would repeat for the life of the session.
    if (!m_depthWarned.contains(id)) {
        m_depthWarned.insert(id);
        qWarning() << "PageRegistry: could not resolve the ancestor chain for" << id
                   << (hops >= MaxParentChainHops ? "— nested deeper than the hop cap" : "— a parentId did not resolve")
                   << "— treating it as hidden";
    }
    return false;
}

bool PageRegistry::pageAllowedInCurrentMode(const QString& id) const
{
    return allowedInMode(id, m_showAdvanced);
}

bool PageRegistry::validateCounterparts() const
{
    bool ok = true;
    for (const Entry& e : m_pages) {
        if (e.counterpartId.isEmpty()) {
            continue;
        }
        if (e.counterpartId == e.id) {
            qWarning() << "PageRegistry: page" << e.id << "declares itself as its own counterpart";
            ok = false;
            continue;
        }
        const auto it = m_indexById.constFind(e.counterpartId);
        if (it == m_indexById.constEnd()) {
            qWarning() << "PageRegistry: page" << e.id << "declares counterpart" << e.counterpartId
                       << "which is not registered — mode flips and deep links will fall back instead of redirecting";
            ok = false;
            continue;
        }
        const Entry& other = m_pages.at(it.value());
        // Counterparts are the two modes' faces of ONE surface, so the mapping
        // has to hold in both directions. A one-way declaration redirects the
        // outbound flip correctly and then dead-ends the return flip on the
        // generic fallback — which looks exactly like correct operation from
        // the outbound side, so nothing else would ever surface it.
        if (other.counterpartId != e.id) {
            qWarning() << "PageRegistry: page" << e.id << "declares counterpart" << e.counterpartId
                       << "but that page declares"
                       << (other.counterpartId.isEmpty() ? QStringLiteral("none") : other.counterpartId)
                       << "— counterparts must point back at each other or the return mode flip falls back";
            ok = false;
        }
        // Same tier means the flip lands on something the new mode hides too,
        // so the redirect cannot help. Always a declaration bug.
        if (other.visibility == e.visibility) {
            qWarning() << "PageRegistry: page" << e.id << "and its counterpart" << e.counterpartId
                       << "share a visibility tier — the counterpart cannot be the other mode's face of this page";
            ok = false;
            // Stop here. Every remaining check is downstream of a tier
            // relationship already known broken, and the reachability block
            // below would re-derive the same fact and report it as an ANCESTOR
            // filtering the counterpart out — sending a maintainer hunting for
            // a filtered category that need not exist. Reciprocity is checked
            // ABOVE rather than below, because it is orthogonal to tier: a pair
            // that is both same-tier and one-way has two independent faults and
            // must report both, or the maintainer fixes one, re-runs, and
            // discovers the other.
            continue;
        }
        // A counterpart with no QML of its own is a category: the mode gate
        // would redirect onto a page with no body, the same silent degradation
        // this validator exists to catch, one level down.
        if (other.qmlSource.isEmpty()) {
            qWarning() << "PageRegistry: page" << e.id << "declares counterpart" << e.counterpartId
                       << "which is not navigable (no QML of its own) — a redirect would land on an empty page";
            ok = false;
            // Same reason the same-tier branch stops: a category has no QML of
            // its own, so if it is also unreachable in the flipped mode the
            // block below would report a SECOND warning for the one declaration
            // fault already named here, sending a maintainer looking for a
            // filtering ancestor on top of a counterpart that cannot be a
            // redirect target at all.
            continue;
        }
        {
            // Opposite tiers are necessary but NOT sufficient. The gate this
            // validator protects asks pageAllowedInCurrentMode(counterpart),
            // which walks ANCESTORS too, so a counterpart whose own tier is
            // right but which sits under a filtered category still dead-ends on
            // the app fallback — exactly the silent degradation this exists to
            // catch. Check reachability in the mode that hides `e`: a
            // SimpleOnly page is hidden when advanced, and vice versa.
            // Which mode hides `e`. For a tiered entry that is its own tier;
            // for an Always entry (never hidden by its OWN tier, but hideable
            // by a filtered ancestor) fall back to the counterpart's tier,
            // which names the mode the pair exists to bridge.
            const bool modeThatHidesThis = e.visibility != PageVisibility::Always
                ? (e.visibility == PageVisibility::SimpleOnly)
                : (other.visibility == PageVisibility::AdvancedOnly);
            if (!allowedInMode(e.counterpartId, modeThatHidesThis)) {
                qWarning() << "PageRegistry: page" << e.id << "declares counterpart" << e.counterpartId
                           << "which is itself unreachable in the mode that hides" << e.id
                           << "— an ancestor category filters it out, so the redirect falls back anyway";
                ok = false;
            }
        }
    }
    return ok;
}

QString PageRegistry::firstVisibleLeafId(const QString& parentId) const
{
    const auto it = m_childrenByParent.constFind(parentId);
    if (it == m_childrenByParent.constEnd()) {
        return {};
    }
    for (const qsizetype idx : it.value()) {
        const Entry& e = m_pages.at(idx);
        if (!isEntryVisible(e)) {
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

QString PageRegistry::parentIdOf(const QString& id) const
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        return {};
    }
    return m_pages.at(it.value()).parentId;
}

QList<PageRegistry::Entry> PageRegistry::topLevelPages() const
{
    // Top-level entries are the empty-key bucket of the child index, in
    // registration order — the same list childPages() reads for any other
    // parent, so this is childPages(QString()) with the id spelled out.
    return childPages(QString());
}

QList<PageRegistry::Entry> PageRegistry::childPages(const QString& parentId) const
{
    QList<Entry> out;
    const auto it = m_childrenByParent.constFind(parentId);
    if (it == m_childrenByParent.constEnd()) {
        return out;
    }
    // Exact reserve rather than a whole-catalogue upper bound: the index knows
    // the child count up front. That matters because a recursive per-node
    // caller runs this once per node, and a whole-catalogue reserve there would
    // allocate room for every page to hold a handful of children, over and over.
    out.reserve(it.value().size());
    for (const qsizetype idx : it.value()) {
        out.append(m_pages.at(idx));
    }
    return out;
}

QList<PageRegistry::Entry> PageRegistry::visibleChildPages(const QString& parentId) const
{
    QList<Entry> out;
    const auto it = m_childrenByParent.constFind(parentId);
    if (it == m_childrenByParent.constEnd()) {
        return out;
    }
    // Exact reserve for the same reason as childPages: this is called once per
    // node per sidebar rebuild (SidebarRows walks it per node, on every search
    // keystroke) and typically returns 1-5 entries. The visibility filter can
    // only shrink the result, so the child count is a tight upper bound.
    out.reserve(it.value().size());
    for (const qsizetype idx : it.value()) {
        const Entry& e = m_pages.at(idx);
        if (isEntryVisible(e)) {
            out.append(e);
        }
    }
    return out;
}

QList<PageRegistry::Entry> PageRegistry::allPages() const
{
    return m_pages;
}

const QList<PageRegistry::Entry>& PageRegistry::allPagesRef() const
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

} // namespace

QLatin1String PageRegistry::titleKey()
{
    return EntryKeys::Title;
}

namespace {

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
    // The empty key holds the top-level entries, so this is the childPagesData
    // of the root — same index bucket, same registration order.
    return childPagesData(QString());
}

QVariantList PageRegistry::childPagesData(const QString& parentId) const
{
    QVariantList out;
    const auto it = m_childrenByParent.constFind(parentId);
    if (it == m_childrenByParent.constEnd()) {
        return out;
    }
    // Exact reserve for the same reason as childPages: the index knows the
    // child count, and the visibility filter can only shrink the result.
    out.reserve(it.value().size());
    for (const qsizetype idx : it.value()) {
        const Entry& e = m_pages.at(idx);
        if (isEntryVisible(e)) {
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
