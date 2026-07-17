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
 * NO dependency on Phosphor config or core layers, so it can move
 * cleanly into the future libs/phosphor-tiles library without dragging
 * cross-layer headers along.
 *
 * Non-algorithm consumers (`src/dbus/autotileadaptor`, `src/core/geometryutils`,
 * etc.) that genuinely need these symbols include this header directly. There
 * is no transitive re-export from `core/constants.h` — that backward-source
 * compatibility chain was removed so unrelated layers no longer resolve the
 * PhosphorTiles include path.
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
/// Window cap for a scripted algorithm whose metadata omits `defaultMaxWindows`.
/// Scripted layouts carry more windows comfortably than the built-in default of
/// 5, so they resolve to this instead. Shared with the settings app's blank
/// scaffold, which writes this value into every algorithm it generates.
constexpr int ScriptedDefaultMaxWindows = 6;
inline constexpr QLatin1String DefaultAlgorithmId{"bsp"}; ///< Default tiling algorithm
constexpr qreal DefaultSplitRatioStep = 0.05;
constexpr qreal MinSplitRatioStep = 0.01;
constexpr qreal MaxSplitRatioStep = 0.25;

constexpr qreal MinSplitRatio = 0.1;
constexpr qreal MaxSplitRatio = 0.9;
constexpr int MinMasterCount = 1;
constexpr int MaxMasterCount = 5;
constexpr int DefaultInnerGap = 8;
constexpr int DefaultOuterGap = 8;
constexpr int MinGap = 0;
constexpr int MaxGap = 50;
constexpr int MinRectSizePx = 50;
constexpr int GapEdgeThresholdPx = 5;
/// Minimum pixel movement of a window edge during an interactive resize before
/// it is treated as an intentional resize (vs. fractional-scale rounding residue
/// or sub-pixel jitter). A separate constant from GapEdgeThresholdPx (currently
/// the same value) so it can be tuned for 1.5×/1.75× fractional scaling later
/// without perturbing gap snapping.
constexpr int ResizeEdgeMoveThresholdPx = 5;
constexpr int MinMaxWindows = 1;
constexpr int MaxMaxWindows = 12;
// Sentinel returned by PerScreenConfigResolver::effectiveMaxWindows() when the
// user has selected AutotileOverflowBehavior::Unlimited. INT_MAX/2 — not
// INT_MAX — so any caller that does `effectiveMaxWindows(...) + 1` (e.g.
// growth-headroom calculations) can't overflow.
constexpr int UnlimitedMaxWindowsSentinel = std::numeric_limits<int>::max() / 2;
constexpr int MaxZones = 256;
constexpr int MaxRuntimeTreeDepth = 50; ///< Maximum recursion depth for split tree operations
// Bounds for the opaque per-algorithm script-state bag (TilingState::scriptState).
// Enforced by TilingState::sanitizeScriptState at every script write-back and on
// load, so a buggy or hostile algorithm can't bloat the persisted state or stall
// serialization. A whole bag exceeding the byte cap is dropped (reset to empty).
constexpr int ScriptStateMaxBytes = 64 * 1024; ///< Max compact-JSON size of the bag
constexpr int ScriptStateMaxDepth = 16; ///< Max object/array nesting depth
constexpr int ScriptStateMaxKeys = 4096; ///< Max total object keys across the bag
constexpr qreal SplitRatioHysteresis = 0.05; ///< Band within which algorithm-switch ratio reset is suppressed
constexpr int MinMetadataWindows = 1;
constexpr int MaxMetadataWindows = 100;
/// Largest algorithm script the engine will load. LuauTileAlgorithm refuses
/// anything bigger, so a writer that lands a larger file on disk leaves the
/// user an algorithm that never appears. Shared so the settings app's import
/// can reject it up front instead.
constexpr qint64 MaxScriptSizeBytes = 1024 * 1024;
/// Prefix of the id a scripted algorithm gets when its metadata declares none:
/// this plus the file's base name. Shared because the loader builds that id for
/// the registry while LuauTileAlgorithm builds its own and strips this back off
/// for the display-name fallback, and the two must agree on the spelling.
inline constexpr QLatin1String ScriptIdPrefix{"script:"};
constexpr int MinInsertPosition = 0;
constexpr int MaxInsertPosition = 2;
// Bounds for AutotileOverflowBehavior (Float=0 .. Unlimited=1), defined below.
// Kept in lockstep with that enum so a per-screen override clamp has a named
// range like MinInsertPosition/MaxInsertPosition rather than a bare literal.
constexpr int MinOverflowBehavior = 0;
constexpr int MaxOverflowBehavior = 1;
// Animation duration + stagger limits previously lived here for
// historical reasons but are NOT autotile-specific — they bound every
// animation in the system. Moved to
// libs/phosphor-animation/include/PhosphorAnimation/AnimationLimits.h
// (PhosphorAnimation::Limits namespace). Consumers that need them
// should include that header directly; ConfigDefaults already does.
/// Watchdog deadline for a single Luau call (tile / lifecycle hook) issued via
/// LuauEngine::callModule. Generous enough for ARM / slow systems where
/// first-call warmup can take tens of milliseconds. Exposed here so operators
/// tuning for their target hardware don't need to recompile the binding TU.
constexpr int ScriptWatchdogTimeoutMs = 100;

/// Returns true if typeId is a numeric QMetaType (Double, Float, Int, UInt, LongLong, ULongLong).
/// Two uses: gating untrusted Luau metadata/override returns before they are
/// accepted as numbers (the primary use since the finiteNumber validation),
/// and fuzzy-comparing QVariant values after JSON round-trip type drift.
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
inline constexpr QLatin1String SplitTreeKey{"splitTree"};
inline constexpr QLatin1String ScriptStateKey{"scriptState"};
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

enum class AutotileOverflowBehavior {
    Float = 0,
    Unlimited = 1
};

enum class AutotileInsertPosition {
    End = 0,
    AfterFocused = 1,
    AsMaster = 2
};

enum class AutotileDragBehavior {
    Float = 0,
    Reorder = 1
};

} // namespace PhosphorTiles
