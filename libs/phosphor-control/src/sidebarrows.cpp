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

// Depth bound for every walk here. Defence in depth against a misregistered
// page graph that names itself as its own ancestor: without it a search
// keystroke would spin forever through the ring. Deliberately larger than
// ApplicationController's 32-hop parent-chain cap because these walks DESCEND
// a tree (depth scales with nesting) while that one walks a single chain.
constexpr int kMaxWalkDepth = 64;

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
constexpr QLatin1String kPageId{"pageId"};
constexpr QLatin1String kTitle{"title"};
constexpr QLatin1String kIconSource{"iconSource"};
constexpr QLatin1String kHasQmlSource{"hasQmlSource"};
constexpr QLatin1String kDepth{"_depth"};
constexpr QLatin1String kIsCollapsibleHeader{"_isCollapsibleHeader"};
constexpr QLatin1String kIsDrillParent{"_isDrillParent"};
constexpr QLatin1String kIsExpanded{"_isExpanded"};
constexpr QLatin1String kIsDivider{"_isDivider"};

QVariantMap makeRow(const QString& pageId, const QString& title, const QString& iconSource, bool hasQmlSource,
                    int depth, bool isCollapsibleHeader, bool isDrillParent, bool isExpanded, bool isDivider)
{
    QVariantMap row;
    row.insert(kPageId, pageId);
    row.insert(kTitle, title);
    row.insert(kIconSource, iconSource);
    row.insert(kHasQmlSource, hasQmlSource);
    row.insert(kDepth, depth);
    row.insert(kIsCollapsibleHeader, isCollapsibleHeader);
    row.insert(kIsDrillParent, isDrillParent);
    row.insert(kIsExpanded, isExpanded);
    row.insert(kIsDivider, isDivider);
    return row;
}

QVariantMap makeDivider(const QString& pageId, int depth)
{
    return makeRow(pageId, QString(), QString(), false, depth, false, false, false, true);
}

bool rowIsDivider(const QVariantMap& row)
{
    return row.value(kIsDivider).toBool();
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
    m_registry = registry;
    Q_EMIT registryChanged();
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
            if (depth > kMaxWalkDepth) {
                qWarning() << "SidebarRows: page tree nested deeper than" << kMaxWalkDepth << "levels under id"
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
                if (depth > kMaxWalkDepth) {
                    qWarning() << "SidebarRows: page tree nested deeper than" << kMaxWalkDepth << "levels under id"
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
                        QList<PageRegistry::Entry> found;
                        const std::function<void(const QString&, int)> gather = [&](const QString& pid, int d) {
                            if (d > kMaxWalkDepth || found.size() > 1) {
                                return;
                            }
                            const QList<PageRegistry::Entry> sub = m_registry->visibleChildPages(pid);
                            for (const PageRegistry::Entry& g : sub) {
                                if (found.size() > 1) {
                                    break;
                                }
                                if (!g.qmlSource.isEmpty()) {
                                    found.append(g);
                                }
                                gather(g.id, d + 1);
                            }
                        };
                        gather(child.id, 0);

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
    const std::function<PageRegistry::Entry(const QString&, int)> findFirstNavigable =
        [&](const QString& parentId, int depth) -> PageRegistry::Entry {
        if (depth > kMaxWalkDepth) {
            return {};
        }
        const QList<PageRegistry::Entry> kids = m_registry->visibleChildPages(parentId);
        for (const PageRegistry::Entry& child : kids) {
            if (!child.qmlSource.isEmpty()) {
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
    // walk above. visibleChildPages is a full scan of m_pages with an ancestor
    // walk per entry, and the caller has already fetched this exact list to
    // compute the has-children predicate — search runs on every keystroke, so
    // fetching it twice per node made the walk quadratic in the catalogue for
    // no new information.
    const std::function<void(const QString&, const QList<PageRegistry::Entry>&, const QString&, int)> collect =
        [&](const QString& parentId, const QList<PageRegistry::Entry>& kids, const QString& breadcrumb, int depth) {
            if (depth > kMaxWalkDepth) {
                qWarning() << "SidebarRows: page tree nested deeper than" << kMaxWalkDepth << "levels under id"
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
                    const PageRegistry::Entry landing = findFirstNavigable(child.id, depth + 1);
                    // Reached only in FLAT mode, and only for a category whose
                    // descendants carry a flatTitleOverrides entry. In tree mode a
                    // leaf's breadcrumb is `ancestor / leaf`, so any needle
                    // matching this category also matches every descendant, each of
                    // which emitted its own (more precise) row and populated
                    // `emitted` during the recursion above — the guard below then
                    // always fires. A flat override REPLACES the breadcrumb, so
                    // there the descendant can fail to match while the category
                    // matches, and this row is the only thing that would offer the
                    // destination. Kept for that case; it is not dead.
                    if (!landing.id.isEmpty() && !emitted.contains(landing.id)) {
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

    // build() applies the descendant count ONLY under `isDrill && !rowHasQml`,
    // so match its entry conditions before matching its walk, or this function
    // answers a different question than the one it exists to agree with.
    const PageRegistry::Entry scope = m_registry->entry(currentParentId);
    if (!scope.qmlSource.isEmpty()) {
        // A category with a page of its own is offered as a drill target
        // unconditionally, whatever its descendant count.
        return currentParentId;
    }
    if (scope.isCollapsible) {
        // Collapsible categories render as accordion headers and are never
        // drill targets, so the rail cannot be inside one to begin with.
        return QString();
    }

    // Mirror build()'s drill rule EXACTLY, which counts to two:
    //   0 navigable descendants -> the category is not offered at all,
    //   1                       -> the row is flattened to that descendant and
    //                              is NOT a drill target (the drill step would
    //                              be pure friction),
    //   2 or more               -> a real drill target.
    // So anything below two means the rail must not be sitting inside this
    // scope. Counting only to one would leave the rail drilled into a category
    // that build() renders as a single direct row at the level above, which is
    // the disagreement this function exists to prevent.
    //
    // Depth-capped like every other walk here, and the cap is fail-CLOSED: a
    // tree deep enough to hit it falls back to the top level rather than
    // stranding the rail in a scope we could not finish verifying.
    int found = 0;
    const std::function<void(const QString&, int)> gather = [&](const QString& pid, int d) {
        if (found > 1 || d > kMaxWalkDepth) {
            return;
        }
        const QList<PageRegistry::Entry> kids = m_registry->visibleChildPages(pid);
        for (const PageRegistry::Entry& child : kids) {
            if (found > 1) {
                return;
            }
            if (!child.qmlSource.isEmpty()) {
                ++found;
            }
            gather(child.id, d + 1);
        }
    };
    gather(currentParentId, 0);
    return found > 1 ? currentParentId : QString();
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
