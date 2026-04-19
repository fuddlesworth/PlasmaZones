// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Picker-composition helpers for the unified layout list.
//
// A "unified layout" is one row in the layout-picker UI — it can back either
// a manual zone-based layout OR an autotile algorithm, and the composition
// functions stitch both sources into a single sorted list for the overlay /
// zone selector / D-Bus layout list.
//
// The canonical entry type is @c PhosphorLayout::LayoutPreview (from
// phosphor-layout-api). All helpers in this header operate on it directly —
// there is no separate app-layer mirror struct.
//
// Lives OUTSIDE libs/ because it pulls in autotile's PhosphorTiles::
// AlgorithmRegistry to compose algorithm entries and needs PlasmaZones-side
// `IOrderingSettings` for custom sort ordering. Keeping the autotile
// coupling here lets the phosphor-zones library stay pure zone/layout
// primitives.

#include "plasmazones_export.h"

#include <PhosphorLayoutApi/LayoutPreview.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PhosphorLayout {
class ILayoutSource;
}

namespace PhosphorZones {
class ILayoutManager;
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
 * @brief Build list of all available layouts (manual, and optionally autotile)
 *
 * When @p includeAutotile is true the helper needs a way to enumerate
 * autotile previews. It picks the input as follows:
 *   1. @p autotileSource — a long-lived @c PhosphorLayout::ILayoutSource
 *      (typically the autotile source owned by a
 *      @c PhosphorLayout::LayoutSourceBundle) whose internal preview cache
 *      is reused across calls. Pass the composition root's bundle source
 *      here when available — this is the fast path.
 *   2. @p algorithmRegistry — fallback. When @p autotileSource is null
 *      the helper constructs a transient @c AutotileLayoutSource over the
 *      registry for this one call. Cache is discarded between calls.
 * Either must be non-null when @p includeAutotile is true; the registry
 * is acceptable for code paths that don't yet hold a bundle reference.
 */
PLASMAZONES_EXPORT QVector<PhosphorLayout::LayoutPreview>
buildUnifiedLayoutList(PhosphorZones::ILayoutManager* layoutManager,
                       PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, bool includeAutotile = false,
                       const QStringList& customOrder = {}, PhosphorLayout::ILayoutSource* autotileSource = nullptr);

/**
 * @brief Build filtered list of layouts visible in the given context
 *
 * Filters out layouts that are:
 * - hiddenFromSelector = true
 * - Not allowed on the given screen/desktop/activity (if allow lists are non-empty)
 * - Not matching the screen's aspect ratio class (if layout has an aspectRatioClass tag)
 *
 * Layouts tagged with a non-matching aspect ratio class are not removed entirely;
 * they are moved to the end of the list so the selector can show them in a
 * collapsed "Other" section. The `recommended` field in the returned entry
 * indicates whether the layout matches the current screen's aspect ratio.
 *
 * See the non-filtered overload for @p autotileSource / @p algorithmRegistry
 * semantics — same fallback rules apply.
 */
PLASMAZONES_EXPORT QVector<PhosphorLayout::LayoutPreview>
buildUnifiedLayoutList(PhosphorZones::ILayoutManager* layoutManager,
                       PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, const QString& screenId,
                       int virtualDesktop, const QString& activity, bool includeManual = true,
                       bool includeAutotile = true, qreal screenAspectRatio = 0.0, bool filterByAspectRatio = false,
                       const QStringList& customOrder = {}, PhosphorLayout::ILayoutSource* autotileSource = nullptr);

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
