// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "constants.h"
#include "plasmazones_export.h"
#include "types.h"
#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorZones/GeometryUtils.h>
#include <PhosphorProtocol/ZoneTypes.h>
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
class IZoneLayoutRegistry;
struct ContextGapOverride;
}

namespace PhosphorScreens {
class ScreenManager;
}

namespace PlasmaZones {

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
 * Unified helper that resolves virtual screen geometry via PhosphorScreens::ScreenManager when
 * available, falling back to the physical QScreen* geometry. Eliminates the
 * repeated pattern of querying PhosphorScreens::ScreenManager + branching on vsGeom.isValid()
 * that appears in navigation, resnap, snap-all, rotation, and overlay code.
 *
 * @param zone PhosphorZones::Zone to get geometry for
 * @param screen Physical QScreen* (used as fallback)
 * @param screenId Screen identifier (physical or virtual)
 * @param layout PhosphorZones::Layout for gap overrides
 * @param settings Global settings for gap fallbacks
 * @param layoutRegistry Optional registry; when non-null, the context-rule gap
 *        override for @p screenId in the registry's CURRENT desktop/activity is
 *        resolved and applied as the highest-precedence gap layer — so preview /
 *        query geometry matches the commit-time geometry the resolver produces.
 *        Pass null (default) to skip rule gaps entirely (legacy behaviour).
 * @return Snapped integer geometry with appropriate gaps applied
 */
PLASMAZONES_EXPORT QRect getZoneGeometryForScreen(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                  QScreen* screen, const QString& screenId,
                                                  PhosphorZones::Layout* layout, ISettings* settings,
                                                  PhosphorZones::IZoneLayoutRegistry* layoutRegistry = nullptr);

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
 * @param layoutRegistry Optional registry; see getZoneGeometryForScreen — when
 *        non-null, the current-context rule gap override for @p screenId is
 *        applied as the highest-precedence gap layer.
 * @return Floating-point geometry with appropriate gaps applied
 */
PLASMAZONES_EXPORT QRectF getZoneGeometryForScreenF(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                                    QScreen* screen, const QString& screenId,
                                                    PhosphorZones::Layout* layout, ISettings* settings,
                                                    PhosphorZones::IZoneLayoutRegistry* layoutRegistry = nullptr);

/**
 * @brief Get effective zone padding for a layout
 * @param layout PhosphorZones::Layout to get padding for (may have per-layout override)
 * @param settings Global settings (used if layout has no override)
 * @return Effective zone padding in pixels
 *
 * @param ruleGapOverride Optional PerScreenSnappingKey-shaped map of context-rule
 *        gap overrides (built from the resolved context rules — per-screen,
 *        per-desktop, per-activity — by DaemonGeometryResolver on the commit path
 *        or by currentContextGapOverride on the preview/query path); takes
 *        precedence over every other tier. Per-screen gaps are themselves rules
 *        now, so they arrive through this override — there is no separate
 *        per-screen Settings tier.
 *
 * Resolution cascade: context-rule override → per-layout override → global
 * default (settings->innerGap(), which reads the Gaps config group) → compile default
 */
PLASMAZONES_EXPORT int getEffectiveInnerGap(PhosphorZones::Layout* layout, ISettings* settings,
                                            const QVariantMap& ruleGapOverride = {});

using ::PhosphorZones::GeometryUtils::snapToRect;

/**
 * @brief Get effective per-side outer gaps for a layout
 * @param layout PhosphorZones::Layout to get gaps for (may have per-layout overrides)
 * @param settings Global settings (used if layout has no override)
 * @return Effective per-side edge gaps
 *
 * @param ruleGapOverride Optional PerScreenSnappingKey-shaped map of context-rule
 *        gap overrides (per-screen/desktop/activity rules); takes precedence over
 *        every other tier. Per-screen gaps are rules now, so they arrive through
 *        this override — there is no separate per-screen Settings tier.
 *
 * Resolution cascade: context-rule override → layout per-side → layout uniform →
 * global per-side → global uniform → compile default
 */
PLASMAZONES_EXPORT ::PhosphorLayout::EdgeGaps getEffectiveOuterGaps(PhosphorZones::Layout* layout, ISettings* settings,
                                                                    const QVariantMap& ruleGapOverride = {});

/**
 * @brief Convert a resolved ContextGapOverride into the PerScreenSnappingKey-shaped
 *        QVariantMap consumed by getEffectiveInnerGap / getEffectiveOuterGaps as
 *        the `ruleGapOverride` argument. Only the override's set (engaged) fields
 *        are written; an empty override yields an empty map.
 *
 * Shared by DaemonGeometryResolver (commit path) and the preview/query geometry
 * helpers above so the rule→map translation lives in exactly one place.
 */
PLASMAZONES_EXPORT QVariantMap contextGapOverrideMap(const PhosphorZones::ContextGapOverride& gaps);

/**
 * @brief Merge the config per-monitor gap override for @p screenId UNDER a
 *        context-rule gap override, so a user gap RULE wins over the config
 *        per-monitor default.
 *
 * Starts from @p settings->perScreenGapOverrides(screenId) (empty when @p settings
 * is null or the monitor has no config gap override), then inserts every entry of
 * @p ruleOverride on top. Both maps are keyed in the same short engine form
 * (InnerGap / OuterGap / …), so the result is a plain per-key union: config fills
 * the tier-1 override for a monitor with a config gap, a rule overrides it per
 * key, and a monitor with neither yields an empty map that falls through to the
 * global config gap.
 */
PLASMAZONES_EXPORT QVariantMap mergeConfigPerScreenGaps(QVariantMap ruleOverride, const ISettings* settings,
                                                        const QString& screenId);

/**
 * @brief Resolve the context-rule gap override for @p screenId in @p reg's
 *        CURRENT desktop/activity, merged with the config per-monitor gap
 *        override, as the PerScreenSnappingKey-shaped map that getEffectiveInnerGap
 *        / getEffectiveOuterGaps consume as `ruleGapOverride`.
 *
 * Returns the config per-monitor gap (from @p settings) when there is no matching
 * context rule, an empty map when neither is present (so the caller falls through
 * to the per-layout/global cascade), or the per-key union with the rule winning
 * when both exist. The registry's current context is kept in sync with the
 * compositor by the daemon, so the override matches the one DaemonGeometryResolver
 * computes on the commit path. This is the preview/query-path counterpart to
 * DaemonGeometryResolver's contextGapOverrideFor — both translate the resolved
 * context rules (merged with config per-monitor gaps) into the same map so
 * preview/selector geometry matches commit-time geometry.
 */
PLASMAZONES_EXPORT QVariantMap currentContextGapOverride(PhosphorZones::IZoneLayoutRegistry* reg,
                                                         const ISettings* settings, const QString& screenId);

using ::PhosphorZones::GeometryUtils::effectiveScreenGeometry;

using ::PhosphorZones::GeometryUtils::extractZoneGeometry;
using ::PhosphorZones::GeometryUtils::setZoneGeometry;

/**
 * @brief Build typed list of empty zones for Snap Assist
 * @param layout PhosphorZones::Layout containing zones
 * @param screen Screen to calculate geometry for
 * @param settings Settings for zone padding/outer gap
 * @param isZoneEmpty Predicate: returns true if zone has no windows
 * @return PhosphorProtocol::EmptyZoneList of empty zone entries with overlay-local geometry
 *
 * Used by PhosphorPlacement::WindowTrackingService::getEmptyZones and WindowDragAdaptor::dragStopped
 * to avoid duplicating the empty-zones building logic.
 */
PLASMAZONES_EXPORT PhosphorProtocol::EmptyZoneList
buildEmptyZoneList(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Layout* layout, QScreen* screen,
                   ISettings* settings, const std::function<bool(const PhosphorZones::Zone*)>& isZoneEmpty,
                   PhosphorZones::IZoneLayoutRegistry* layoutRegistry = nullptr);

/**
 * @brief Build typed list of empty zones using explicit screen ID (virtual-screen-aware)
 *
 * Uses PhosphorScreens::ScreenManager to resolve virtual screen geometry when available, falling back
 * to the physical QScreen* geometry.
 */
PLASMAZONES_EXPORT PhosphorProtocol::EmptyZoneList
buildEmptyZoneList(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Layout* layout, const QString& screenId,
                   QScreen* physScreen, ISettings* settings,
                   const std::function<bool(const PhosphorZones::Zone*)>& isZoneEmpty,
                   PhosphorZones::IZoneLayoutRegistry* layoutRegistry = nullptr);

using ::PhosphorGeometry::enforceMinSizes;
using ::PhosphorGeometry::removeRectOverlaps;

using ::PhosphorEngine::GeometryUtils::deserializeZoneAssignments;
using ::PhosphorEngine::GeometryUtils::serializeZoneAssignments;
using ::PhosphorGeometry::rectToJson;

} // namespace GeometryUtils

} // namespace PlasmaZones
