// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorcontrol_export.h"

#include <QObject>
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
     *                      Ignored while @p searchText is non-empty.
     * @param searchText    Filter needle. Empty disables filtering.
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

Q_SIGNALS:
    void registryChanged();

private:
    QObject* m_registryGuard = nullptr;
    PageRegistry* m_registry = nullptr;
};

} // namespace PhosphorControl
