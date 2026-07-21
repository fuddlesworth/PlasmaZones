// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/SidebarRows.h"

#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/SearchRanker.h"

#include <QDebug>
#include <QSet>

#include <functional>

namespace PhosphorControl {

namespace {

// Nesting-depth bound for every walk here. Not a cycle guard: registerPage
// requires a child's parent to be registered already, so the graph is acyclic
// by construction and a descent always terminates (PageRegistry's
// MaxParentChainHops documents the same reasoning for the upward walk). This
// only bounds pathological nesting, and is deliberately larger than
// ApplicationController's 32-hop parent-chain cap because these walks DESCEND
// a tree (depth scales with nesting) while that one walks a single chain.
constexpr int MaxWalkDepth = 64;

// The one place a flat-mode title override is applied. Both the flat rail walk
// and the Q_INVOKABLE Breadcrumbs calls go through here, so the rail and the
// crumb can never disagree about a leaf's flat title.
QString flatTitleOf(const QString& pageId, const QString& registeredTitle, const QVariantMap& flatTitleOverrides)
{
    const auto it = flatTitleOverrides.constFind(pageId);
    return it == flatTitleOverrides.constEnd() ? registeredTitle : it.value().toString();
}

// Role keys. Declared once so a typo cannot silently produce a row the
// delegate reads as undefined.
constexpr QLatin1String PageIdKey{"pageId"};
constexpr QLatin1String TitleKey{"title"};
constexpr QLatin1String IconSourceKey{"iconSource"};
constexpr QLatin1String HasQmlSourceKey{"hasQmlSource"};
constexpr QLatin1String DepthKey{"_depth"};
constexpr QLatin1String IsCollapsibleHeaderKey{"_isCollapsibleHeader"};
constexpr QLatin1String IsDrillParentKey{"_isDrillParent"};
constexpr QLatin1String IsExpandedKey{"_isExpanded"};
constexpr QLatin1String IsDividerKey{"_isDivider"};

QVariantMap makeRow(const QString& pageId, const QString& title, const QString& iconSource, bool hasQmlSource,
                    int depth, bool isCollapsibleHeader, bool isDrillParent, bool isExpanded, bool isDivider)
{
    QVariantMap row;
    row.insert(PageIdKey, pageId);
    row.insert(TitleKey, title);
    row.insert(IconSourceKey, iconSource);
    row.insert(HasQmlSourceKey, hasQmlSource);
    row.insert(DepthKey, depth);
    row.insert(IsCollapsibleHeaderKey, isCollapsibleHeader);
    row.insert(IsDrillParentKey, isDrillParent);
    row.insert(IsExpandedKey, isExpanded);
    row.insert(IsDividerKey, isDivider);
    return row;
}

QVariantMap makeDivider(const QString& pageId, int depth)
{
    return makeRow(pageId, QString(), QString(), false, depth, false, false, false, true);
}

bool rowIsDivider(const QVariantMap& row)
{
    return row.value(IsDividerKey).toBool();
}

// An absent id counts as EXPANDED: the rail starts open, so only an explicit
// false collapses. Mirrors Sidebar's _isExpanded.
bool isExpanded(const QVariantMap& expandedCategories, const QString& id)
{
    const auto it = expandedCategories.constFind(id);
    if (it == expandedCategories.constEnd()) {
        return true;
    }
    return it.value() != QVariant(false);
}

// The descendant walk behind SidebarRows::firstTwoNavigableDescendants, taking
// the scope's visible children as a parameter so a caller that has already
// fetched them does not pay for the subtree-descending visibility test twice.
// See the member function for what the count means to each caller.
QList<PageRegistry::Entry> firstTwoNavigableUnder(const PageRegistry* registry, const QList<PageRegistry::Entry>& kids)
{
    QList<PageRegistry::Entry> found;
    const std::function<void(const QList<PageRegistry::Entry>&, int)> gather =
        [&](const QList<PageRegistry::Entry>& sub, int d) {
            for (const PageRegistry::Entry& g : sub) {
                if (found.size() > 1) {
                    return;
                }
                if (!g.qmlSource.isEmpty()) {
                    found.append(g);
                    if (found.size() > 1) {
                        return;
                    }
                }
                // Warn like the other four walks in this file. A truncated
                // count is not a cosmetic omission here: it flips a real drill
                // target into "not enterable", so build() drops the row and
                // resolveDrillScope evicts the rail out of the scope. Silence
                // would leave nothing in the log explaining either.
                if (d + 1 > MaxWalkDepth) {
                    qWarning() << "SidebarRows: page tree nested deeper than" << MaxWalkDepth << "levels under id"
                               << g.id
                               << "— descendants below this point are not counted, so the category may not be offered "
                                  "as a drill target";
                    continue;
                }
                gather(registry->visibleChildPages(g.id), d + 1);
            }
        };
    gather(kids, 0);
    return found;
}

} // namespace

SidebarRows::SidebarRows(QObject* parent)
    : QObject(parent)
{
}

SidebarRows::~SidebarRows() = default;

QString SidebarRows::dividerPrefix()
{
    return QStringLiteral("__divider__");
}

PageRegistry* SidebarRows::registry() const
{
    return m_registry;
}

void SidebarRows::setRegistry(PageRegistry* registry)
{
    if (m_registry == registry) {
        return;
    }
    disconnect(m_registryDestroyed);
    m_registry = registry;
    if (registry != nullptr) {
        // Relay the registry's death as a property change. m_registry is a
        // QPointer, so it self-nulls when the owning ApplicationController is
        // torn down, but silently: a QML binding on `registry` would keep the
        // stale value while build() had already begun returning an empty list.
        // Signal-to-signal, so the binding re-evaluates and reads null.
        m_registryDestroyed = connect(registry, &QObject::destroyed, this, &SidebarRows::registryChanged);
    }
    Q_EMIT registryChanged();
}

QList<PageRegistry::Entry> SidebarRows::firstTwoNavigableDescendants(const QString& parentId) const
{
    // Stops at TWO because both callers only need to distinguish
    // zero / exactly-one / more-than-one:
    //   0 -> the drill leads nowhere (a row whose only content would be a
    //        Back button), so build() does not offer it and resolveDrillScope
    //        evicts the rail out of it,
    //   1 -> the drill step is pure friction, so build() flattens the row to
    //        that descendant and it is NOT a drill target,
    //   2+ -> a real drill target.
    //
    // ONE implementation on purpose. build() and resolveDrillScope have to
    // agree about what "enterable" means, and the lib has no QML test harness,
    // so two copies of this walk would ship green the moment they diverged —
    // which is exactly what happened when resolveDrillScope was first written
    // with its own copy that counted to one. resolveDrillScope reaches the walk
    // through firstTwoNavigableUnder directly, because it has already fetched
    // the scope's children; the walk itself is still the single shared one.
    if (!m_registry) {
        return {};
    }
    return firstTwoNavigableUnder(m_registry, m_registry->visibleChildPages(parentId));
}

QVariantList SidebarRows::build(bool flattenTree, const QString& searchText, const QString& currentParentId,
                                const QVariantMap& expandedCategories, const QVariantMap& flatTitleOverrides) const
{
    QVariantList out;
    if (m_registry == nullptr) {
        return out;
    }

    const QString prefix = dividerPrefix();

    // Folded ONCE, above the mode selection, and every branch below tests THIS
    // rather than the raw text. Mode selection used to test the untrimmed
    // string while the search walk trimmed and folded, so a query of only
    // spaces (or only combining marks, via dead keys / IME) satisfied neither:
    // it skipped flat/tree because the raw text was non-empty, then folded to
    // nothing and returned an empty list. The whole navigation rail went blank
    // with nothing on screen explaining why. "No usable query" now means the
    // same thing to all three walks.
    const QString needle = SearchRanker::foldForSearch(searchText.trimmed());

    // ── FLAT ────────────────────────────────────────────────────────────
    // One list of every visible navigable page, walked from the ROOT (drill
    // scope is meaningless here), depth 0 throughout, in registration order. A
    // TOP-LEVEL entry's hasDividerAfter fires after the last row its SUBTREE
    // emitted, so the tree's section seams survive flattening; leaf-level flags
    // are ignored because they are tuned for the tree rail's within-category
    // rhythm and would put a seam after nearly every row. Consecutive and
    // trailing dividers collapse, since intervening rows may have filtered out.
    if (flattenTree && needle.isEmpty()) {
        QSet<QString> seen;
        seen.insert(QString());

        const std::function<void(const QString&, int)> emitLeaves = [&](const QString& parentId, int depth) {
            if (depth > MaxWalkDepth) {
                qWarning() << "SidebarRows: page tree nested deeper than" << MaxWalkDepth << "levels under id"
                           << parentId << "— rows below this point are omitted from the rail";
                return;
            }
            const QList<PageRegistry::Entry> kids = m_registry->visibleChildPages(parentId);
            for (const PageRegistry::Entry& child : kids) {
                if (seen.contains(child.id)) {
                    continue;
                }
                seen.insert(child.id);
                const int before = out.size();
                if (!child.qmlSource.isEmpty()) {
                    out.append(makeRow(child.id, flatTitleOf(child.id, child.title, flatTitleOverrides),
                                       child.iconSource, true, 0, false, false, false, false));
                }
                emitLeaves(child.id, depth + 1);

                const bool emittedAny = out.size() > before;
                const bool lastIsDivider = !out.isEmpty() && rowIsDivider(out.last().toMap());
                if (depth == 0 && child.hasDividerAfter && emittedAny && !lastIsDivider) {
                    out.append(makeDivider(prefix + QStringLiteral("flat/") + child.id, 0));
                }
            }
        };
        emitLeaves(QString(), 0);

        while (!out.isEmpty() && rowIsDivider(out.last().toMap())) {
            out.removeLast();
        }
        return out;
    }

    // ── TREE ────────────────────────────────────────────────────────────
    if (needle.isEmpty()) {
        // Flat seen-set over every VISITED id (parent and child), seeded with
        // the scope root. A parent-id-only guard would miss sibling-level
        // duplicates, because each parent walk starts with only its own marker.
        QSet<QString> seen;
        seen.insert(currentParentId);

        const std::function<void(const QString&, int, const QList<PageRegistry::Entry>&)> walk =
            [&](const QString& parentId, int depth, const QList<PageRegistry::Entry>& kids) {
                if (depth > MaxWalkDepth) {
                    qWarning() << "SidebarRows: page tree nested deeper than" << MaxWalkDepth << "levels under id"
                               << parentId << "— rows below this point are omitted from the rail";
                    return;
                }
                for (const PageRegistry::Entry& child : kids) {
                    if (seen.contains(child.id)) {
                        continue;
                    }
                    seen.insert(child.id);

                    // One lookup per child, reused for the has-children
                    // predicate AND the recursion.
                    const QList<PageRegistry::Entry> childKids = m_registry->visibleChildPages(child.id);
                    const bool childHasChildren = !childKids.isEmpty();
                    const bool collapsible = child.isCollapsible && childHasChildren;

                    QString rowPageId = child.id;
                    bool rowHasQml = !child.qmlSource.isEmpty();
                    bool isDrill = !collapsible && childHasChildren;

                    if (isDrill && !rowHasQml) {
                        // Collect up to two navigable descendants: zero means
                        // the drill leads nowhere (a row whose only content
                        // would be a Back button), exactly one means the drill
                        // step is pure friction and the row should navigate
                        // straight there, keeping the category's title/icon.
                        const QList<PageRegistry::Entry> found = firstTwoNavigableDescendants(child.id);

                        if (found.isEmpty()) {
                            continue;
                        }
                        if (found.size() == 1) {
                            rowPageId = found.first().id;
                            rowHasQml = true;
                            isDrill = false;
                            seen.insert(found.first().id);
                        }
                    }

                    const bool expanded = collapsible && isExpanded(expandedCategories, child.id);
                    out.append(makeRow(rowPageId, child.title, child.iconSource, rowHasQml, depth, collapsible, isDrill,
                                       expanded, false));

                    if (expanded) {
                        walk(child.id, depth + 1, childKids);
                    }

                    // Section seam. The pageId carries parentId + child.id so
                    // it stays unique even with identical labels under
                    // different parents.
                    if (child.hasDividerAfter) {
                        out.append(makeDivider(prefix + parentId + QStringLiteral("/") + child.id, depth));
                    }
                }
            };
        walk(currentParentId, 0, m_registry->visibleChildPages(currentParentId));

        // Same trailing-divider trim the flat walk applies, for the same
        // reason: the rows a seam was meant to separate can filter out. Under
        // `animations` the simple-mode page carries the seam and every sibling
        // after it is advanced-only, so in simple mode the divider is the last
        // row emitted and the rail would draw a separator under nothing.
        while (!out.isEmpty() && rowIsDivider(out.last().toMap())) {
            out.removeLast();
        }
        return out;
    }

    // ── SEARCH ──────────────────────────────────────────────────────────
    // A flat match list from every scope. Dividers are suppressed: they carry
    // no match metadata and would break the result list's reading order.
    QSet<QString> seen;
    // Destinations already offered. A child's breadcrumb carries its ancestors'
    // titles, so a needle matching a CATEGORY title also matches every
    // descendant breadcrumb: the leaves emit their own rows, and then the
    // category wants to emit a landing row aimed at findFirstNavigable() —
    // which is one of those very leaves. Two rows, one destination, different
    // titles. The leaf's own row wins because it names the destination exactly
    // ("Snapping / Behavior"), where the category's names only the ancestor
    // ("Snapping") and relies on the reader knowing where it lands.
    QSet<QString> emitted;

    // Walk down until a navigable descendant appears, so a category-only
    // parent whose TITLE matches still routes the user somewhere useful.
    // Descendants already in `emitted` are skipped rather than returned: their
    // own row already offers that destination, and returning one would make the
    // caller's duplicate guard drop the category's landing row entirely even
    // though a second, un-emitted navigable descendant could have served it.
    // An emitted navigable child is still RECURSED INTO — the candidate may be
    // below it.
    const std::function<PageRegistry::Entry(const QString&, int)> findFirstNavigable =
        [&](const QString& parentId, int depth) -> PageRegistry::Entry {
        if (depth > MaxWalkDepth) {
            qWarning() << "SidebarRows: page tree nested deeper than" << MaxWalkDepth << "levels under id" << parentId
                       << "— no landing destination is looked for below this point, so the category's search result is "
                          "dropped";
            return {};
        }
        const QList<PageRegistry::Entry> kids = m_registry->visibleChildPages(parentId);
        for (const PageRegistry::Entry& child : kids) {
            if (!child.qmlSource.isEmpty() && !emitted.contains(child.id)) {
                return child;
            }
            const PageRegistry::Entry desc = findFirstNavigable(child.id, depth + 1);
            if (!desc.id.isEmpty()) {
                return desc;
            }
        }
        return {};
    };

    // Takes `kids` by parameter rather than re-fetching, mirroring the tree
    // walk above. visibleChildPages copies an Entry per child and runs the
    // visibility test (which descends the subtree) on each, and the caller has
    // already fetched this exact list to compute the has-children predicate —
    // search runs on every keystroke, so fetching it twice per node doubled the
    // walk for no new information.
    const std::function<void(const QString&, const QList<PageRegistry::Entry>&, const QString&, int)> collect =
        [&](const QString& parentId, const QList<PageRegistry::Entry>& kids, const QString& breadcrumb, int depth) {
            if (depth > MaxWalkDepth) {
                qWarning() << "SidebarRows: page tree nested deeper than" << MaxWalkDepth << "levels under id"
                           << parentId << "— matches below this point are omitted from the results";
                return;
            }
            for (const PageRegistry::Entry& child : kids) {
                if (seen.contains(child.id)) {
                    continue;
                }
                seen.insert(child.id);

                const QList<PageRegistry::Entry> grandKids = m_registry->visibleChildPages(child.id);
                const bool grand = !grandKids.isEmpty();

                // A flat-mode OVERRIDDEN id must read the same as its rail row:
                // take the override and DROP the ancestor breadcrumb, since the
                // override exists precisely because the registered title reads
                // wrong without its ancestors — which is what a breadcrumb would
                // restore. Non-overridden rows keep their breadcrumb even though
                // the flat rail shows none: search results span every scope, so
                // ancestor context is the only thing telling same-named leaves
                // apart.
                const auto override_ = flatTitleOverrides.constFind(child.id);
                const bool flatOverridden = flattenTree && override_ != flatTitleOverrides.constEnd();
                const QString childBreadcrumb = flatOverridden
                    ? override_.value().toString()
                    : (breadcrumb.isEmpty() ? child.title : breadcrumb + QStringLiteral(" / ") + child.title);

                // Always recurse so descendants can match. grandKids is handed
                // down rather than re-fetched inside.
                if (grand) {
                    collect(child.id, grandKids, childBreadcrumb, depth + 1);
                }

                // Folded on both sides via the SAME helper the global index uses, so
                // the rail and global search agree on what "matches".
                const bool matchesNeedle = SearchRanker::foldForSearch(childBreadcrumb).contains(needle);
                if (!child.qmlSource.isEmpty()) {
                    if (matchesNeedle) {
                        out.append(
                            makeRow(child.id, childBreadcrumb, child.iconSource, true, 0, false, false, false, false));
                        emitted.insert(child.id);
                    }
                } else if (matchesNeedle && grand) {
                    // findFirstNavigable skips ids already in `emitted`, so this
                    // is an un-offered destination or nothing at all. In tree
                    // mode a leaf's breadcrumb is `ancestor / leaf`, so any
                    // needle matching this category also matches every
                    // descendant, each of which emitted its own (more precise)
                    // row during the recursion above — the walk then usually
                    // finds nothing and no landing row is added. A flat override
                    // REPLACES the breadcrumb, so there a descendant can fail to
                    // match while the category matches, and this row is the only
                    // thing that would offer the destination.
                    const PageRegistry::Entry landing = findFirstNavigable(child.id, depth + 1);
                    if (!landing.id.isEmpty()) {
                        out.append(makeRow(landing.id, childBreadcrumb, landing.iconSource, true, 0, false, false,
                                           false, false));
                        emitted.insert(landing.id);
                    }
                }
            }
        };
    // Walk from the ROOT, not from currentParentId: search spans every scope
    // regardless of how deep the user has drilled. Rooting it at the drill
    // parent silently narrows the results to the current category while the
    // rail still looks top-level — the Back button that would explain the
    // scope is hidden whenever a search is active (Sidebar.qml) — so a user
    // inside "Snapping" searching for a Rules page would get an empty list and
    // no reason why. It would also make the ancestor breadcrumb above
    // pointless: every result would share the same prefix.
    collect(QString(), m_registry->visibleChildPages(QString()), QString(), 0);
    return out;
}

QString SidebarRows::resolveDrillScope(const QString& currentParentId) const
{
    if (currentParentId.isEmpty() || !m_registry) {
        return QString();
    }
    if (!m_registry->pageAllowedInCurrentMode(currentParentId)) {
        return QString();
    }

    // Mirror build()'s decision for this node EXACTLY, in build()'s own order.
    // build() computes:
    //     collapsible = isCollapsible && childHasChildren
    //     isDrill     = !collapsible && childHasChildren
    // and only then, under `isDrill && !rowHasQml`, runs the descendant count.
    // Every one of those clauses matters here:
    //   - childHasChildren FIRST, because a node with no visible children is a
    //     plain leaf row, never a scope,
    //   - collapsible BEFORE qmlSource, because build() gives it precedence: a
    //     collapsible category renders as an accordion header and is never a
    //     drill target, whatever qmlSource it carries,
    //   - qmlSource next, because a category with a page of its own is offered
    //     as a drill target without consulting its descendant count,
    //   - the count last, where 0 or 1 both mean "not an enterable scope".
    //
    // The child list is fetched ONCE and handed to the descendant walk below:
    // visibleChildPages runs the visibility test (which descends the subtree)
    // per child, and the walk's first act would otherwise be to fetch this
    // exact list again.
    const QList<PageRegistry::Entry> kids = m_registry->visibleChildPages(currentParentId);
    if (kids.isEmpty()) {
        return QString();
    }
    const PageRegistry::Entry scope = m_registry->entry(currentParentId);
    if (scope.isCollapsible) {
        return QString();
    }
    if (!scope.qmlSource.isEmpty()) {
        return currentParentId;
    }
    return firstTwoNavigableUnder(m_registry, kids).size() > 1 ? currentParentId : QString();
}

QVariantMap SidebarRows::flatPageData(const QString& pageId, const QVariantMap& flatTitleOverrides) const
{
    if (m_registry == nullptr) {
        return {};
    }
    QVariantMap data = m_registry->pageData(pageId);
    if (data.isEmpty()) {
        // Unknown id — hand back the empty map rather than a dict carrying only
        // an overridden title, so the caller's "no such page" check still works.
        return data;
    }
    // PageRegistry's own key, not this file's row-role constant: this dict is
    // registry page-data, a different schema from the sidebar row.
    const QLatin1String titleKey = PageRegistry::titleKey();
    data.insert(titleKey, flatTitleOf(pageId, data.value(titleKey).toString(), flatTitleOverrides));
    return data;
}

} // namespace PhosphorControl
