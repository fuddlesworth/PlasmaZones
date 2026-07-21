// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorcontrol_export.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorControl {

class PageRegistry;

/**
 * @brief Derives the sidebar's visible row list from the page registry.
 *
 * Three walks over the tier-filtered tree, one per rail mode:
 *   - FLAT: every visible navigable page as one depth-0 list, no headers or
 *     drill steps. Only TOP-LEVEL entries contribute section seams.
 *   - TREE: the children of the current drill scope, with collapsible
 *     accordion headers, drill parents, and per-entry seams.
 *   - SEARCH: a flat match list from every scope, each row titled with its
 *     ancestor breadcrumb.
 *
 * Lives in C++ rather than in Sidebar.qml so the walks are directly
 * unit-testable without a Qt Quick engine or a built page tree — the same
 * split, for the same reason, as animationpagescope / decorationpagescope on
 * the app side. Sidebar.qml keeps the genuinely view-side work (the incremental
 * ListModel diff, drill animation, expand state).
 *
 * ROW ROLE CONTRACT — the public model schema consumers bind to via
 * `modelData.<role>`. Renaming any of these breaks downstream trailing
 * delegates:
 *   - `pageId`, `title`, `iconSource`, `hasQmlSource` — page identity.
 *   - `_depth`, `_isCollapsibleHeader`, `_isDrillParent`, `_isExpanded`,
 *     `_isDivider` — underscore-prefixed layout/state hints. View-internal by
 *     convention (do not store or derive from them), but part of the contract
 *     because a consumer's trailingDelegate must read them to render badges
 *     and active stripes.
 *
 * `pageId` (not `id`) is deliberate: `id` would shadow QML's id: directive in
 * the delegate and trips qmlformat's parser.
 */
class PHOSPHORCONTROL_EXPORT SidebarRows : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SidebarRows)
    Q_PROPERTY(PhosphorControl::PageRegistry* registry READ registry WRITE setRegistry NOTIFY registryChanged)

public:
    explicit SidebarRows(QObject* parent = nullptr);
    ~SidebarRows() override;

    PageRegistry* registry() const;
    void setRegistry(PageRegistry* registry);

    /** The prefix stamped onto a synthetic divider row's pageId. Shared by the
     *  producers here and by the navigation guard that must refuse to route
     *  such a row into the controller — a rename reaching only the producers
     *  would send a divider straight to currentPageId. */
    Q_INVOKABLE static QString dividerPrefix();

    /**
     * Build the visible row list.
     *
     * @param flattenTree   Render one flat depth-0 list instead of the tree.
     *                      Still honoured while searching: a flat-mode search
     *                      returns flat rows with their overridden titles.
     * @param searchText    Filter needle. Filtering is disabled when it is
     *                      empty AFTER trimming and search-folding, so a query
     *                      of only whitespace or only combining marks falls
     *                      back to the normal rail rather than matching
     *                      nothing.
     * @param currentParentId  Drill scope for TREE mode; empty is top level.
     * @param expandedCategories  id → expanded. An ABSENT id counts as
     *                      expanded, matching the rail's open-by-default
     *                      behaviour, so only explicit `false` collapses.
     * @param flatTitleOverrides  id → display title, consulted in FLAT mode
     *                      (and by a flat-mode search match) for leaves whose
     *                      registered tree-context title reads wrong alone.
     */
    Q_INVOKABLE QVariantList build(bool flattenTree, const QString& searchText, const QString& currentParentId,
                                   const QVariantMap& expandedCategories, const QVariantMap& flatTitleOverrides) const;

    /** Registry page-data for @p pageId with its flat-mode title override
     *  applied, or an empty map for an unknown id.
     *
     *  Exists so Breadcrumbs resolves the override through the SAME code the
     *  rail uses. It previously re-implemented the lookup in QML JS (down to
     *  its own hasOwnProperty guard against ids colliding with
     *  Object.prototype names), which meant two implementations of one rule,
     *  the QML one uncovered — the lib has no QML test harness, so a
     *  divergence between them would ship green. QVariantMap has no prototype,
     *  so the guard the JS needed is structurally unnecessary here. */
    Q_INVOKABLE QVariantMap flatPageData(const QString& pageId, const QVariantMap& flatTitleOverrides) const;

    /** The drill scope the rail should actually be in, given @p currentParentId.
     *
     *  Returns @p currentParentId when the rail can still render it, and an
     *  empty string when it must fall back to the top level. A scope stops
     *  being renderable in two ways, and the rail is wrong in a different way
     *  for each: if the tier filter hides the parent itself, the rail keeps
     *  rendering a scope the mode has abolished; if the filter hides every
     *  navigable descendant instead, the parent survives and the rail collapses
     *  to a lone Back button over an empty list. build() already refuses to
     *  OFFER a category that leads nowhere, so a rail that stays inside one is
     *  inconsistent with the rows it draws.
     *
     *  Lives here rather than in QML because it is the same rule build() walks,
     *  and the lib has no QML test harness, so a second copy in JS would ship
     *  green when it diverged. */
    Q_INVOKABLE QString resolveDrillScope(const QString& currentParentId) const;

Q_SIGNALS:
    void registryChanged();

private:
    /// QPointer, not a raw pointer: the registry is set from QML
    /// (`registry: root.controller.registry`) and is owned by the
    /// ApplicationController, not by this object. If that controller is torn
    /// down first, a raw pointer would leave build() dereferencing freed
    /// memory behind a null check that still reads as non-null. QPointer makes
    /// the existing `m_registry == nullptr` guard actually true in that case.
    QPointer<PageRegistry> m_registry;
};

} // namespace PhosphorControl
