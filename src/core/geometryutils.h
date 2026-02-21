// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "constants.h"
#include "plasmazones_export.h"
#include <QRectF>
#include <QScreen>
#include <QVariantMap>
#include <functional>
#include <QVector>
#include <QSize>

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
 * @brief Convert QRectF to QRect with edge-consistent rounding
 * @param rf Source floating-point rectangle
 * @return Integer rectangle with consistent edge rounding
 *
 * Unlike QRectF::toRect() which rounds x, y, width, height independently,
 * this rounds the edges (left, top, right, bottom) and derives width/height
 * from the rounded edges. This ensures adjacent zones sharing an edge always
 * produce exactly the configured gap between them, even when fractional
 * scaling (e.g. 1.2x) produces non-integer zone boundaries.
 */
PLASMAZONES_EXPORT QRect snapToRect(const QRectF& rf);

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
PLASMAZONES_EXPORT QString buildEmptyZonesJson(Layout* layout, QScreen* screen, ISettings* settings,
                                               const std::function<bool(const Zone*)>& isZoneEmpty);

/**
 * @brief Enforce minimum size constraints on zones by borrowing space from neighbors
 * @param zones List of zone geometries to adjust (in-place)
 * @param minSizes List of minimum sizes for each zone (same index as zones)
 * @param gapThreshold Threshold for considering zones effectively adjacent
 * @param innerGap Desired gap between adjacent zones (preserved during overlap resolution)
 *
 * Checks if any zone is smaller than its minimum size. If so, attempts to
 * expand it by shrinking adjacent neighbors proportionally. When multiple
 * windows have minimum size, overlaps can occur; a final pass removes them.
 */
PLASMAZONES_EXPORT void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes,
                                               int gapThreshold, int innerGap = 0);

/**
 * @brief Remove overlapping zone rectangles so no two zones intersect
 * @param zones List of zone geometries to adjust (in-place)
 * @param minSizes Per-zone minimum sizes (optional); when resolving overlaps,
 *        prefer shrinking the zone with more surplus above its minimum
 * @param innerGap Desired gap between adjacent zones; when resolving overlaps,
 *        the boundary is offset so zones maintain this gap instead of being flush
 *
 * When multiple zones have minimum size constraints, steal logic can leave
 * boundaries inconsistent. This fixes horizontal and vertical overlaps by
 * shifting the shared edge toward the zone with more surplus, respecting
 * minimum sizes so enforcement is not undone.
 */
PLASMAZONES_EXPORT void removeZoneOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes = {},
                                            int innerGap = 0);

} // namespace GeometryUtils

} // namespace PlasmaZones
