// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayoutApi/GapKeys.h>
#include <QLatin1String>
#include <QMetaType>
#include <limits>

/**
 * @file AutotileConstants.h
 * @brief Algorithm-layer constants for the autotile/tile primitives.
 *
 * This header owns the JSON keys and numeric defaults that the tiling
 * algorithm primitives need to be self-contained.  It intentionally has
 * NO dependency on PlasmaZones config or core layers, so it can move
 * cleanly into the future libs/phosphor-tiles library without dragging
 * cross-layer headers along.
 *
 * Non-algorithm consumers (`src/dbus/autotileadaptor`, `src/core/geometryutils`,
 * etc.) reach these symbols transparently via `core/constants.h`, which
 * re-includes this header for backward source compatibility.
 */
namespace PhosphorTiles {

/**
 * @brief Auto-tiling algorithm defaults
 *
 * Min/max constraints used by the tiling algorithms themselves.  These are
 * algorithm-layer concerns — independent of any user-facing default values
 * the application config layer may surface.
 */
namespace AutotileDefaults {
// Default values used by tiling primitives when no setting is supplied.
// The application config layer surfaces these as the user-facing defaults
// (see ConfigDefaults::autotile{SplitRatio,MasterCount,MaxWindows} in
// src/config/configdefaults.h, which delegate here).
constexpr qreal DefaultSplitRatio = 0.5; ///< 50/50 split when nothing else specified
constexpr int DefaultMasterCount = 1; ///< Single master window
constexpr int DefaultMaxWindows = 5; ///< Maximum tiled windows before overflow
inline constexpr QLatin1String DefaultAlgorithmId{"bsp"}; ///< Default tiling algorithm
constexpr qreal DefaultSplitRatioStep = 0.05;
constexpr qreal MinSplitRatioStep = 0.01;
constexpr qreal MaxSplitRatioStep = 0.25;

constexpr qreal MinSplitRatio = 0.1;
constexpr qreal MaxSplitRatio = 0.9;
constexpr int MinMasterCount = 1;
constexpr int MaxMasterCount = 5;
constexpr int MinGap = 0;
constexpr int MaxGap = 50;
constexpr int MinZoneSizePx = 50;
constexpr int GapEdgeThresholdPx = 5;
constexpr int MinMaxWindows = 1;
constexpr int MaxMaxWindows = 12;
// Sentinel returned by PerScreenConfigResolver::effectiveMaxWindows() when the
// user has selected AutotileOverflowBehavior::Unlimited. INT_MAX/2 — not
// INT_MAX — so any caller that does `effectiveMaxWindows(...) + 1` (e.g.
// growth-headroom calculations) can't overflow.
constexpr int UnlimitedMaxWindowsSentinel = std::numeric_limits<int>::max() / 2;
constexpr int MaxZones = 256;
constexpr int MaxRuntimeTreeDepth = 50; ///< Maximum recursion depth for split tree operations
/// Cap on total node count when converting a SplitTree to a QJSValue object. Each
/// tiled window occupies one leaf plus at most one internal node, so MaxZones*2
/// is a safe ceiling that lets the widest valid tree through without truncation.
constexpr int MaxTreeNodesForJs = MaxZones * 2;
constexpr qreal SplitRatioHysteresis = 0.05; ///< Band within which algorithm-switch ratio reset is suppressed
constexpr int MinMetadataWindows = 1;
constexpr int MaxMetadataWindows = 100;
constexpr int MinInsertPosition = 0;
constexpr int MaxInsertPosition = 2;
constexpr int MinAnimationDuration = 50;
constexpr int MaxAnimationDuration = 500;
constexpr int MinAnimationStaggerIntervalMs = 10;
constexpr int MaxAnimationStaggerIntervalMs = 200;
/// Watchdog deadline for a single JS evaluation guarded by
/// ScriptedAlgorithm::guardedCall(). Generous enough for ARM / slow
/// systems where JS startup and first-call JIT warmup can take tens of
/// milliseconds. Exposed here so operators tuning for their target
/// hardware don't need to recompile the scriptedalgorithm TU.
constexpr int ScriptWatchdogTimeoutMs = 100;

/// Returns true if typeId is a numeric QMetaType (Double, Float, Int, UInt, LongLong, ULongLong).
/// Used for fuzzy-comparing QVariant values after JSON round-trip type drift.
constexpr bool isNumericMetaType(int typeId)
{
    return typeId == QMetaType::Double || typeId == QMetaType::Float || typeId == QMetaType::Int
        || typeId == QMetaType::UInt || typeId == QMetaType::LongLong || typeId == QMetaType::ULongLong;
}
} // namespace AutotileDefaults

/**
 * @brief JSON keys for autotile state serialization
 */
namespace AutotileJsonKeys {
// TilingState keys
inline constexpr QLatin1String ScreenName{"screenName"};
inline constexpr QLatin1String WindowOrder{"windowOrder"};
inline constexpr QLatin1String FloatingWindows{"floatingWindows"};
inline constexpr QLatin1String FocusedWindow{"focusedWindow"};
inline constexpr QLatin1String MasterCount{"masterCount"};
inline constexpr QLatin1String SplitRatio{"splitRatio"};
inline constexpr QLatin1String SplitRatioStep{"splitRatioStep"};
inline constexpr QLatin1String CustomParams{"customParams"};

// AutotileConfig keys
inline constexpr QLatin1String PerAlgorithmSettings{"perAlgorithmSettings"};
inline constexpr QLatin1String AlgorithmId{"algorithmId"};
inline constexpr QLatin1String InnerGap{"innerGap"};
inline constexpr QLatin1String OuterGap{"outerGap"};

// Per-side outer-gap keys — canonical definition in phosphor-layout-api's
// GapKeys.h (shared with phosphor-zones ZoneJsonKeys). Re-exported here so
// AutotileJsonKeys callers can keep their fully-qualified references
// unchanged while both libraries consume a single source of truth.
using PhosphorLayout::GapKeys::OuterGapBottom; ///< canonical: PhosphorLayoutApi/GapKeys.h
using PhosphorLayout::GapKeys::OuterGapLeft; ///< canonical: PhosphorLayoutApi/GapKeys.h
using PhosphorLayout::GapKeys::OuterGapRight; ///< canonical: PhosphorLayoutApi/GapKeys.h
using PhosphorLayout::GapKeys::OuterGapTop; ///< canonical: PhosphorLayoutApi/GapKeys.h
using PhosphorLayout::GapKeys::UsePerSideOuterGap; ///< canonical: PhosphorLayoutApi/GapKeys.h

inline constexpr QLatin1String SmartGaps{"smartGaps"};
inline constexpr QLatin1String FocusNewWindows{"focusNewWindows"};
inline constexpr QLatin1String FocusFollowsMouse{"focusFollowsMouse"};
inline constexpr QLatin1String InsertPosition{"insertPosition"};
inline constexpr QLatin1String RespectMinimumSize{"respectMinimumSize"};
inline constexpr QLatin1String MaxWindows{"maxWindows"};
inline constexpr QLatin1String OverflowBehavior{"overflowBehavior"};
inline constexpr QLatin1String CenteredMasterSplitRatio{"centeredMasterSplitRatio"};
inline constexpr QLatin1String CenteredMasterMasterCount{"centeredMasterMasterCount"};
inline constexpr QLatin1String SplitTreeKey{"splitTree"};
} // namespace AutotileJsonKeys

/**
 * @brief JSON value strings (enum-ish discriminants) paired with the keys above.
 *
 * Split out from @ref AutotileJsonKeys so consumers can disambiguate between
 * "the config key string" and "a value that gets stored under that key". Keeps
 * the `using namespace` ergonomics used by callers like AutotileConfig.cpp.
 */
namespace AutotileJsonValues {
// OverflowBehavior values
inline constexpr QLatin1String OverflowFloat{"float"};
inline constexpr QLatin1String OverflowUnlimited{"unlimited"};
// InsertPosition values
inline constexpr QLatin1String InsertEnd{"end"};
inline constexpr QLatin1String InsertAfterFocused{"afterFocused"};
inline constexpr QLatin1String InsertAsMaster{"asMaster"};
} // namespace AutotileJsonValues

/**
 * @brief Backwards-compat re-exports so existing `using namespace AutotileJsonKeys`
 *        sites (e.g. `src/autotile/AutotileConfig.cpp`) resolve the value names
 *        without a rename cascade. New code should qualify with AutotileJsonValues::.
 */
namespace AutotileJsonKeys {
using AutotileJsonValues::InsertAfterFocused;
using AutotileJsonValues::InsertAsMaster;
using AutotileJsonValues::InsertEnd;
using AutotileJsonValues::OverflowFloat;
using AutotileJsonValues::OverflowUnlimited;
} // namespace AutotileJsonKeys

enum class AutotileOverflowBehavior {
    Float = 0,
    Unlimited = 1
};

enum class AutotileDragBehavior {
    Float = 0,
    Reorder = 1
};

} // namespace PhosphorTiles
