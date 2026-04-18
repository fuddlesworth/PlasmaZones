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

namespace PhosphorZones {
class ILayoutManager;
class Layout;
}

namespace PlasmaZones {

class IOrderingSettings;

} // namespace PlasmaZones

namespace PhosphorZones::LayoutUtils {

using ::PlasmaZones::IOrderingSettings;

/**
 * @brief Build list of all available layouts (manual, and optionally autotile)
 */
PLASMAZONES_EXPORT QVector<PhosphorLayout::LayoutPreview>
buildUnifiedLayoutList(PhosphorZones::ILayoutManager* layoutManager, bool includeAutotile = false,
                       const QStringList& customOrder = {});

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
 */
PLASMAZONES_EXPORT QVector<PhosphorLayout::LayoutPreview>
buildUnifiedLayoutList(PhosphorZones::ILayoutManager* layoutManager, const QString& screenId, int virtualDesktop,
                       const QString& activity, bool includeManual = true, bool includeAutotile = true,
                       qreal screenAspectRatio = 0.0, bool filterByAspectRatio = false,
                       const QStringList& customOrder = {});

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
