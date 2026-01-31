// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QRectF>
#include <QScreen>
#include <QVariantMap>

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
 * @brief Calculate absolute zone geometry for a screen
 * @param zone Zone to calculate geometry for
 * @param screen Screen to calculate geometry relative to
 * @return Absolute geometry in screen coordinates
 */
PLASMAZONES_EXPORT QRectF calculateZoneGeometry(Zone* zone, QScreen* screen);

/**
 * @brief Clip zone geometry to available screen area (excludes panels/docks)
 * @param zone Zone to clip
 * @param screen Screen to clip relative to
 * @return Clipped geometry
 */
PLASMAZONES_EXPORT QRectF clipZoneToAvailableArea(Zone* zone, QScreen* screen);

/**
 * @brief Clip zone geometry to available area with minimum size check
 * @param zone Zone to clip
 * @param screen Screen to clip relative to
 * @param minSize Minimum display size (from settings)
 * @return Clipped geometry (may be empty if too small)
 */
PLASMAZONES_EXPORT QRectF clipZoneToAvailableArea(Zone* zone, QScreen* screen, int minSize);

/**
 * @brief Convert zone geometry to overlay window local coordinates
 * @param geometry Absolute geometry in screen coordinates
 * @param screen Screen the overlay window is on
 * @return Geometry in overlay window local coordinates
 */
PLASMAZONES_EXPORT QRectF toOverlayCoordinates(const QRectF& geometry, QScreen* screen);

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
 * @brief Get zone geometry with explicit spacing applied (uniform spacing)
 * @param zone Zone to get geometry for
 * @param screen Screen to calculate relative to
 * @param spacing Explicit spacing value in pixels (from settings->zonePadding())
 * @param useAvailableGeometry If true, calculate relative to available area (excluding panels/taskbars)
 * @return Geometry with spacing applied
 */
PLASMAZONES_EXPORT QRectF getZoneGeometryWithSpacing(Zone* zone, QScreen* screen, int spacing,
                                                     bool useAvailableGeometry = true);

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
 * @brief Calculate absolute zone geometry using available screen area
 * @param zone Zone to calculate geometry for
 * @param screen Screen to calculate geometry relative to
 * @return Absolute geometry in screen coordinates within available area
 *
 * This version uses screen->availableGeometry() which excludes panels and taskbars,
 * ensuring zones don't overlap with system UI elements.
 */
PLASMAZONES_EXPORT QRectF calculateZoneGeometryInAvailableArea(Zone* zone, QScreen* screen);

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

} // namespace GeometryUtils

} // namespace PlasmaZones
