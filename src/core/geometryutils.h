// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "constants.h"
#include "plasmazones_export.h"
#include <QRectF>
#include <QScreen>
#include <QVariantMap>
#include <functional>

namespace PlasmaZones {

class Zone;
class Layout;
class ISettings;

/**
 * @brief Centralized geometry calculation utilities
 *
 * Provides common geometry calculations to avoid duplication
 * across OverlayService, OverlayAdaptor, and other components.
 */
namespace GeometryUtils {

/**
 * @brief Convert available-area zone geometry to overlay window local coordinates
 * @param geometry Absolute geometry in available screen coordinates
 * @param screen Screen the overlay window is on
 * @return Geometry in overlay window local coordinates
 *
 * The overlay window covers the full screen, but zones calculated against
 * availableGeometry need to be offset to account for panels/taskbars.
 */
PLASMAZONES_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen);

/**
 * @brief Get zone geometry with differentiated inner/outer gaps
 * @param zone Zone to get geometry for
 * @param screen Screen to calculate relative to
 * @param innerGap Gap between adjacent zones (zonePadding)
 * @param outerGap Gap at screen boundaries
 * @param useAvailableGeometry If true, calculate relative to available area (excluding panels/taskbars)
 * @return Geometry with appropriate gaps applied
 *
 * This version applies outerGap to zone edges that touch screen boundaries
 * (relative position 0 or 1), and innerGap/2 to edges between zones.
 */
PLASMAZONES_EXPORT QRectF getZoneGeometryWithGaps(Zone* zone, QScreen* screen, int innerGap, int outerGap,
                                                   bool useAvailableGeometry = true);

/**
 * @brief Get zone geometry with per-side outer gaps
 * @param zone Zone to get geometry for
 * @param screen Screen to calculate relative to
 * @param innerGap Gap between adjacent zones (zonePadding)
 * @param outerGaps Per-side edge gaps
 * @param useAvailableGeometry If true, calculate relative to available area (excluding panels/taskbars)
 * @return Geometry with appropriate gaps applied
 */
PLASMAZONES_EXPORT QRectF getZoneGeometryWithGaps(Zone* zone, QScreen* screen, int innerGap, const EdgeGaps& outerGaps,
                                                   bool useAvailableGeometry = true);

/**
 * @brief Get effective zone padding for a layout
 * @param layout Layout to get padding for (may have per-layout override)
 * @param settings Global settings (used if layout has no override)
 * @return Effective zone padding in pixels
 *
 * Returns layout-specific zonePadding if set (>= 0), otherwise falls back
 * to global settings->zonePadding(), or default of 8 if settings is null.
 */
PLASMAZONES_EXPORT int getEffectiveZonePadding(Layout* layout, ISettings* settings);

/**
 * @brief Get effective outer gap for a layout
 * @param layout Layout to get outer gap for (may have per-layout override)
 * @param settings Global settings (used if layout has no override)
 * @return Effective outer gap in pixels
 *
 * Returns layout-specific outerGap if set (>= 0), otherwise falls back
 * to global settings->outerGap(), or default of 8.
 *
 * Outer gap is applied to zone edges at screen boundaries (positions 0 or 1),
 * while zonePadding is applied between adjacent zones. Use getZoneGeometryWithGaps()
 * to apply differentiated gaps.
 */
PLASMAZONES_EXPORT int getEffectiveOuterGap(Layout* layout, ISettings* settings);

/**
 * @brief Get effective per-side outer gaps for a layout
 * @param layout Layout to get gaps for (may have per-layout overrides)
 * @param settings Global settings (used if layout has no override)
 * @return Effective per-side edge gaps
 *
 * Resolution cascade: layout per-side → layout uniform → global per-side → global uniform → default
 */
PLASMAZONES_EXPORT EdgeGaps getEffectiveOuterGaps(Layout* layout, ISettings* settings);

/**
 * @brief Get the effective screen geometry for a layout
 * @param layout Layout to check (may use full screen geometry)
 * @param screen Screen to get geometry for
 * @return Full screen geometry if layout->useFullScreenGeometry(), otherwise available geometry
 *
 * Centralizes the decision of whether to use full screen or available (panel-excluded)
 * geometry based on the layout's useFullScreenGeometry setting.
 */
PLASMAZONES_EXPORT QRectF effectiveScreenGeometry(Layout* layout, QScreen* screen);

/**
 * @brief Extract geometry as QRectF from a zone QVariantMap
 * @param zone Zone data map containing x, y, width, height keys
 * @return QRectF with the zone's geometry
 *
 * Used by EditorController and serialization code to avoid repeating
 * the x/y/width/height extraction pattern.
 */
PLASMAZONES_EXPORT QRectF extractZoneGeometry(const QVariantMap& zone);

/**
 * @brief Set geometry fields in a zone QVariantMap from a QRectF
 * @param zone Zone data map to modify
 * @param rect Geometry to set
 *
 * Sets x, y, width, height keys in the zone map.
 */
PLASMAZONES_EXPORT void setZoneGeometry(QVariantMap& zone, const QRectF& rect);

/**
 * @brief Build JSON array of empty zones for Snap Assist
 * @param layout Layout containing zones
 * @param screen Screen to calculate geometry for
 * @param settings Settings for zone padding/outer gap
 * @param isZoneEmpty Predicate: returns true if zone has no windows
 * @return JSON array string (compact format)
 *
 * Used by WindowTrackingService::getEmptyZonesJson and WindowDragAdaptor::dragStopped
 * to avoid duplicating the empty-zones JSON building logic.
 */
PLASMAZONES_EXPORT QString buildEmptyZonesJson(
    Layout* layout, QScreen* screen, ISettings* settings,
    const std::function<bool(const Zone*)>& isZoneEmpty);

} // namespace GeometryUtils

} // namespace PlasmaZones
