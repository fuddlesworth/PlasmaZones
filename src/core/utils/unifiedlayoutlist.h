// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Picker-composition helpers for the unified layout list.
//
// A "unified layout" is one row in the layout-picker UI. It can back a manual
// zone-based layout, an autotile algorithm, or a native scrolling template,
// and the composition functions stitch all three sources into a single sorted
// list for the overlay / zone selector / D-Bus layout list.
//
// The canonical entry type is @c PhosphorLayout::LayoutPreview (from
// phosphor-layout-api). All helpers in this header operate on it directly -
// there is no separate app-layer mirror struct.
//
// Lives OUTSIDE libs/ because it pulls in autotile's PhosphorTiles::
// AlgorithmRegistry to compose algorithm entries and needs PlasmaZones-side
// `IOrderingSettings` for custom sort ordering. Keeping the autotile
// coupling here lets the phosphor-zones library stay pure zone/layout
// primitives.

#include "plasmazones_export.h"

#include <PhosphorLayoutApi/LayoutPreview.h>

#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PhosphorLayout {
class ILayoutSource;
}

namespace PhosphorZones {
class IZoneLayoutRegistry;
class ScrollingTemplateStore;
class Layout;
}

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
}

namespace PlasmaZones {

class IOrderingSettings;

} // namespace PlasmaZones

namespace PhosphorZones::LayoutUtils {

using ::PlasmaZones::IOrderingSettings;

/**
 * @brief Build list of all available layouts (manual, optionally autotile,
 *        and the native scrolling templates when a store is supplied)
 *
 * When @p includeAutotile is true the helper needs a way to enumerate
 * autotile previews. It picks the input as follows:
 *   1. @p autotileSource - a long-lived @c PhosphorLayout::ILayoutSource
 *      (typically the autotile source owned by a
 *      @c PhosphorLayout::LayoutSourceBundle) whose internal preview cache
 *      is reused across calls. Pass the composition root's bundle source
 *      here when available - this is the fast path.
 *   2. @p algorithmRegistry - fallback. When @p autotileSource is null
 *      the helper constructs a transient @c AutotileLayoutSource over the
 *      registry for this one call. Cache is discarded between calls.
 * Either must be non-null when @p includeAutotile is true; the registry
 * is acceptable for code paths that don't yet hold a bundle reference.
 *
 * Scrolling templates have no bool gate on this overload, unlike the
 * context-filtered one below: they are included exactly when @p templateStore
 * is non-null. This overload answers "everything that exists", so a caller
 * that does not want template rows passes no store.
 */
PLASMAZONES_EXPORT QVector<PhosphorLayout::LayoutPreview>
buildUnifiedLayoutList(PhosphorZones::IZoneLayoutRegistry* layoutManager,
                       PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, bool includeAutotile = false,
                       const QStringList& customOrder = {}, PhosphorLayout::ILayoutSource* autotileSource = nullptr,
                       QSize autotilePreviewCanvas = {},
                       PhosphorZones::ScrollingTemplateStore* templateStore = nullptr);

/**
 * @brief Build filtered list of layouts visible in the given context
 *
 * Filters out layouts that are:
 * - hiddenFromSelector = true
 * - Not allowed on the given screen/desktop/activity (if allow lists are non-empty)
 * - Not matching the screen's aspect ratio class, when @p filterByAspectRatio
 *   is true and @p screenAspectRatio is known
 *
 * The context's ACTIVE layout is exempt from every one of these so the selector and
 * cycling can never lose it.
 *
 * @p screenAspectRatio alone only TAGS rows: it sets `recommended` on each
 * entry, which a caller can use to group the mismatched ones. Rows are dropped
 * only when @p filterByAspectRatio is also true.
 *
 * @p includeScrollingTemplates gates the native ScrollingTemplate rows, and
 * @p templateStore supplies them — both are needed for template entries to
 * appear. Unlike the manual layouts above, template rows pass through
 * unfiltered: no allow lists, no hidden flag and no aspect filter apply to
 * them (a column vocabulary is not a placement, so screen shape says nothing
 * about its fit).
 *
 * @p stripVerticalAxis is this screen's strip axis. Template cards are a
 * picture of the columns the strip will hold, so on a screen whose strip runs
 * vertically they have to draw their bands stacked rather than in a row. A
 * caller with no axis source passes false and gets the horizontal depiction,
 * which is what every non-scrolling screen shows anyway.
 *
 * See the non-filtered overload for @p autotileSource / @p algorithmRegistry
 * semantics - same fallback rules apply.
 */
PLASMAZONES_EXPORT QVector<PhosphorLayout::LayoutPreview>
buildUnifiedLayoutList(PhosphorZones::IZoneLayoutRegistry* layoutManager,
                       PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, const QString& screenId,
                       int virtualDesktop, const QString& activity, bool includeManual = true,
                       bool includeAutotile = true, qreal screenAspectRatio = 0.0, bool filterByAspectRatio = false,
                       const QStringList& customOrder = {}, PhosphorLayout::ILayoutSource* autotileSource = nullptr,
                       QSize autotilePreviewCanvas = {}, bool includeScrollingTemplates = false,
                       PhosphorZones::ScrollingTemplateStore* templateStore = nullptr,
                       bool includeNoTemplateRow = false, bool stripVerticalAxis = false);

/**
 * @brief Build a combined custom order list from settings
 */
PLASMAZONES_EXPORT QStringList buildCustomOrder(const IOrderingSettings* settings, bool includeManual,
                                                bool includeAutotile);

/**
 * @brief Find a preview by ID in the list
 * @return Index of found preview, or -1 if not found
 */
PLASMAZONES_EXPORT int findLayoutIndex(const QVector<PhosphorLayout::LayoutPreview>& previews, const QString& layoutId);

/**
 * @brief Get preview by ID
 * @return Pointer to preview if found, nullptr otherwise
 */
PLASMAZONES_EXPORT const PhosphorLayout::LayoutPreview*
findLayout(const QVector<PhosphorLayout::LayoutPreview>& previews, const QString& layoutId);

} // namespace PhosphorZones::LayoutUtils
