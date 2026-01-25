// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QRectF>
#include <QScreen>

namespace PlasmaZones {

class Zone;
class ISettings;

/**
 * @brief Centralized geometry calculation utilities (DRY)
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
 * @brief Get zone geometry with explicit spacing applied (uses global setting)
 * @param zone Zone to get geometry for
 * @param screen Screen to calculate relative to
 * @param spacing Explicit spacing value in pixels (from settings->zonePadding())
 * @param useAvailableGeometry If true, calculate relative to available area (excluding panels/taskbars)
 * @return Geometry with spacing applied
 */
PLASMAZONES_EXPORT QRectF getZoneGeometryWithSpacing(Zone* zone, QScreen* screen, int spacing,
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

} // namespace GeometryUtils

} // namespace PlasmaZones
