// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "constants.h"
#include "plasmazones_export.h"
#include "types.h"
#include <PhosphorZones/GeometryUtils.h>
#include <PhosphorProtocol/WireTypes.h>
#include <QRectF>
#include <QScreen>
#include <QVariantMap>
#include <functional>
#include <QVector>
#include <QSize>
#include <optional>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorZones {
class Layout;
class Zone;
}

namespace Phosphor::Screens {
class ScreenManager;
}

namespace PlasmaZones {

using PhosphorProtocol::EmptyZoneEntry;
using PhosphorProtocol::EmptyZoneList;

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
 * @param zone PhosphorZones::Zone to get geometry for
 * @param screen Screen to calculate relative to
 * @param innerGap Gap between adjacent zones (zonePadding)
 * @param outerGaps Per-side edge gaps
 * @param useAvailableGeometry If true, calculate relative to available area (excluding panels/taskbars)
 * @param screenId Optional virtual screen ID for physical-edge lookup.
 *        When non-empty, used for physicalEdgesFor() so that internal virtual
 *        screen edges get inner gap instead of outer gap. When empty, falls
 *        back to Phosphor::Screens::ScreenIdentity::identifierFor(screen) (physical ID, all edges outer).
 * @return Geometry with appropriate gaps applied
 */
PLASMAZONES_EXPORT QRectF getZoneGeometryWithGaps(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                  QScreen* screen, int innerGap,
                                                  const ::PhosphorLayout::EdgeGaps& outerGaps,
                                                  bool useAvailableGeometry = true, const QString& screenId = {});

/**
 * @brief Convert zone geometry to overlay-local coordinates using explicit geometry
 * @param geometry Absolute geometry in screen coordinates
 * @param overlayGeometry The overlay window's full geometry (origin + size)
 * @return Geometry in overlay window local coordinates
 *
 * For virtual screens, the overlay covers the virtual screen bounds,
 * so we subtract the virtual screen origin to get overlay-local coords.
 */
PLASMAZONES_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, const QRect& overlayGeometry);

/**
 * @brief Get zone geometry with gaps, using explicit screen geometry
 * @param zone PhosphorZones::Zone to get geometry for
 * @param screenGeometry Screen (or virtual screen) geometry in absolute pixels
 * @param availableGeometry Available area within the screen (excluding panels)
 * @param innerGap Gap between adjacent zones (zonePadding)
 * @param outerGaps Per-side edge gaps
 * @param useAvailableGeometry If true, calculate relative to available area
 * @return Geometry with appropriate gaps applied
 */
PLASMAZONES_EXPORT QRectF getZoneGeometryWithGaps(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                  const QRect& screenGeometry, const QRect& availableGeometry,
                                                  int innerGap, const ::PhosphorLayout::EdgeGaps& outerGaps,
                                                  bool useAvailableGeometry = true, const QString& screenId = {});

/**
 * @brief Get zone geometry with gaps, auto-resolving virtual screen geometry
 *
 * Unified helper that resolves virtual screen geometry via Phosphor::Screens::ScreenManager when
 * available, falling back to the physical QScreen* geometry. Eliminates the
 * repeated pattern of querying Phosphor::Screens::ScreenManager + branching on vsGeom.isValid()
 * that appears in navigation, resnap, snap-all, rotation, and overlay code.
 *
 * @param zone PhosphorZones::Zone to get geometry for
 * @param screen Physical QScreen* (used as fallback)
 * @param screenId Screen identifier (physical or virtual)
 * @param layout PhosphorZones::Layout for gap overrides
 * @param settings Global settings for gap fallbacks
 * @return Snapped integer geometry with appropriate gaps applied
 */
PLASMAZONES_EXPORT QRect getZoneGeometryForScreen(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                  QScreen* screen, const QString& screenId,
                                                  PhosphorZones::Layout* layout, ISettings* settings);

/**
 * @brief Get zone geometry with gaps, auto-resolving virtual screen geometry (floating-point)
 *
 * Same as getZoneGeometryForScreen but returns unsnapped QRectF. Use when the
 * caller needs to combine multiple zone geometries (e.g. QRectF::united()) before
 * a final snap, or when floating-point precision is needed for overlay coordinates.
 *
 * @param zone PhosphorZones::Zone to get geometry for
 * @param screen Physical QScreen* (used as fallback)
 * @param screenId Screen identifier (physical or virtual)
 * @param layout PhosphorZones::Layout for gap overrides
 * @param settings Global settings for gap fallbacks
 * @return Floating-point geometry with appropriate gaps applied
 */
PLASMAZONES_EXPORT QRectF getZoneGeometryForScreenF(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                    QScreen* screen, const QString& screenId,
                                                    PhosphorZones::Layout* layout, ISettings* settings);

/**
 * @brief Get effective zone padding for a layout
 * @param layout PhosphorZones::Layout to get padding for (may have per-layout override)
 * @param settings Global settings (used if layout has no override)
 * @param screenId Optional screen identifier for per-screen override lookup
 * @return Effective zone padding in pixels
 *
 * Resolution cascade: per-screen override → layout override → global settings → default (8px)
 */
PLASMAZONES_EXPORT int getEffectiveZonePadding(PhosphorZones::Layout* layout, ISettings* settings,
                                               const QString& screenId = {});

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
 * @param layout PhosphorZones::Layout to get gaps for (may have per-layout overrides)
 * @param settings Global settings (used if layout has no override)
 * @param screenId Optional screen identifier for per-screen override lookup
 * @return Effective per-side edge gaps
 *
 * Resolution cascade: per-screen per-side → per-screen uniform → layout per-side →
 * layout uniform → global per-side → global uniform → default
 */
PLASMAZONES_EXPORT ::PhosphorLayout::EdgeGaps getEffectiveOuterGaps(PhosphorZones::Layout* layout, ISettings* settings,
                                                                    const QString& screenId = {});

/**
 * @brief Get the effective screen geometry for a layout
 * @param layout PhosphorZones::Layout to check (may use full screen geometry)
 * @param screen Screen to get geometry for
 * @return Full screen geometry if layout->useFullScreenGeometry(), otherwise available geometry
 *
 * Centralizes the decision of whether to use full screen or available (panel-excluded)
 * geometry based on the layout's useFullScreenGeometry setting.
 */
PLASMAZONES_EXPORT QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                                                  QScreen* screen);

/**
 * @brief Get the effective screen geometry for a layout using a screen ID
 * @param layout PhosphorZones::Layout to check (may use full screen geometry)
 * @param screenId Screen identifier (physical or virtual, e.g. "physicalId/vs:N")
 * @return Virtual screen geometry if available, otherwise falls back to physical screen geometry
 *
 * Virtual-screen-aware overload: resolves geometry via Phosphor::Screens::ScreenManager first,
 * then falls back to finding the physical QScreen by ID.
 */
PLASMAZONES_EXPORT QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                                                  const QString& screenId);

/**
 * @brief Extract geometry as QRectF from a zone QVariantMap
 * @param zone PhosphorZones::Zone data map containing x, y, width, height keys
 * @return QRectF with the zone's geometry
 *
 * Used by EditorController and serialization code to avoid repeating
 * the x/y/width/height extraction pattern.
 */
PLASMAZONES_EXPORT QRectF extractZoneGeometry(const QVariantMap& zone);

/**
 * @brief Set geometry fields in a zone QVariantMap from a QRectF
 * @param zone PhosphorZones::Zone data map to modify
 * @param rect Geometry to set
 *
 * Sets x, y, width, height keys in the zone map.
 */
PLASMAZONES_EXPORT void setZoneGeometry(QVariantMap& zone, const QRectF& rect);

/**
 * @brief Build typed list of empty zones for Snap Assist
 * @param layout PhosphorZones::Layout containing zones
 * @param screen Screen to calculate geometry for
 * @param settings Settings for zone padding/outer gap
 * @param isZoneEmpty Predicate: returns true if zone has no windows
 * @return EmptyZoneList of empty zone entries with overlay-local geometry
 *
 * Used by WindowTrackingService::getEmptyZones and WindowDragAdaptor::dragStopped
 * to avoid duplicating the empty-zones building logic.
 */
PLASMAZONES_EXPORT EmptyZoneList buildEmptyZoneList(Phosphor::Screens::ScreenManager* mgr,
                                                    PhosphorZones::Layout* layout, QScreen* screen, ISettings* settings,
                                                    const std::function<bool(const PhosphorZones::Zone*)>& isZoneEmpty);

/**
 * @brief Build typed list of empty zones using explicit screen ID (virtual-screen-aware)
 *
 * Uses Phosphor::Screens::ScreenManager to resolve virtual screen geometry when available, falling back
 * to the physical QScreen* geometry.
 */
PLASMAZONES_EXPORT EmptyZoneList buildEmptyZoneList(Phosphor::Screens::ScreenManager* mgr,
                                                    PhosphorZones::Layout* layout, const QString& screenId,
                                                    QScreen* physScreen, ISettings* settings,
                                                    const std::function<bool(const PhosphorZones::Zone*)>& isZoneEmpty);

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
PLASMAZONES_EXPORT void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes, int gapThreshold,
                                              int innerGap = 0);

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

/**
 * @brief Convert QRect to compact JSON string {x, y, width, height}
 * @param rect Rectangle to serialize
 * @return JSON string suitable for D-Bus geometry exchange
 *
 * Shared utility for all components that need to serialize zone/window
 * geometry for D-Bus signals (SnapEngine, WindowTrackingAdaptor, etc.).
 */
PLASMAZONES_EXPORT QString rectToJson(const QRect& rect);

/**
 * @brief Serialize zone assignment entries to compact JSON array
 * @param entries Vector of zone assignment entries (window moves with source/target zones)
 * @return JSON array string suitable for D-Bus signals
 *
 * Shared by SnapEngine navigation (rotate, resnap) and any future code
 * that needs to serialize ZoneAssignmentEntry vectors for D-Bus exchange.
 */
PLASMAZONES_EXPORT QString serializeZoneAssignments(const QVector<ZoneAssignmentEntry>& entries);

} // namespace GeometryUtils

} // namespace PlasmaZones
