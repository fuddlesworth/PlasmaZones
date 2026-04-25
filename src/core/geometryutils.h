// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "constants.h"
#include "plasmazones_export.h"
#include "types.h"
#include <PhosphorEngineApi/GeometryUtils.h>
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

using ::PhosphorZones::GeometryUtils::availableAreaToOverlayCoordinates;

using ::PhosphorZones::GeometryUtils::getZoneGeometryWithGaps;

// getZoneGeometryWithGaps (QRect overload) imported via using above

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

using ::PhosphorZones::GeometryUtils::snapToRect;

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

using ::PhosphorZones::GeometryUtils::effectiveScreenGeometry;

using ::PhosphorZones::GeometryUtils::extractZoneGeometry;
using ::PhosphorZones::GeometryUtils::setZoneGeometry;

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

PLASMAZONES_EXPORT void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes, int gapThreshold,
                                              int innerGap = 0);

PLASMAZONES_EXPORT void removeZoneOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes = {},
                                           int innerGap = 0);

using ::PhosphorEngineApi::GeometryUtils::serializeZoneAssignments;
using ::PhosphorGeometry::rectToJson;

} // namespace GeometryUtils

} // namespace PlasmaZones
