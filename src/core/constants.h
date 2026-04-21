// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QLatin1String>
#include <QMetaType>
#include <QString>
#include <limits>

// Pull in the canonical effect↔daemon protocol-version constants
// (PlasmaZones::DBus::ApiVersion / MinPeerApiVersion). Defined in
// compositor-common so the KWin effect, which does not link
// plasmazones_core, shares exactly one definition with the daemon.
#include <dbus_constants.h>

// Shared shapes (per-side gap struct + aspect-ratio classification) live in
// libs/phosphor-layout-api so every layout/tile provider can consume them
// without forcing them to depend on PlasmaZones internals.  Re-aliased into
// namespace PlasmaZones below.
#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/ZoneDefaults.h>
#include <PhosphorZones/ZoneJsonKeys.h>

// NOTE: PhosphorTiles::AutotileJsonKeys / PhosphorTiles::AutotileDefaults previously lived inline here and
// were re-exported from <PhosphorTiles/AutotileConstants.h> via a transitive
// include.  That transitive chain forced every PlasmaZones consumer (including
// libraries like PhosphorZones that have no use for autotile symbols) to
// resolve the PhosphorTiles include path.  Consumers that genuinely need the
// autotile namespaces now include `<PhosphorTiles/AutotileConstants.h>`
// directly.

namespace PlasmaZones {

// PhosphorZones::ZoneGeometryMode lives in libs/phosphor-zones —
// `PhosphorZones::Zone.h` declares it inside `namespace PlasmaZones` so it's
// visible to existing callers via the same name.  No alias needed here;
// consumers that need the enum include `<PhosphorZones/Zone.h>` directly.

/**
 * @brief Default values for zone appearance and core module constants
 *
 * These defaults are used by core module files that can't depend on config.
 * For user-configurable settings, see ConfigDefaults.
 *
 * PhosphorZones::Layout ratio constants (PriorityGridMainRatio, FocusSideRatio, etc.) are
 * structural constants for built-in layouts.
 */
namespace Defaults {
// Fallback screen dimensions when no QScreen is available
inline constexpr int FallbackScreenWidth = 1920;
inline constexpr int FallbackScreenHeight = 1080;

// PhosphorZones::Zone-presentation defaults + layout-factory ratios live in
// `PhosphorZones::ZoneDefaults` (libs/phosphor-zones).  All in-tree
// callers reference them qualified directly — no using-alias here.

// PlasmaZones-side defaults that aren't part of the zone-presentation
// surface — daemon overlay / settings / geometry constants.
constexpr int ZonePadding = 8;
constexpr int OuterGap = 8; // Gap at screen edges (separate from zonePadding between zones)
constexpr int MaxGap = 50; // Maximum for zone padding and outer gap settings
// EdgeThreshold for overlay window detection (pixels, used in WindowTracker/Overlay)
constexpr qreal EdgeThreshold = 15.0;

// Performance and behavior constants (configurable via Settings)
constexpr int PollIntervalMs = 50; // Window move detection polling interval (20 FPS)
constexpr int MinimumZoneSizePx = 100; // Minimum zone size for window snapping
constexpr int MinimumZoneDisplaySizePx = 10; // Minimum zone size for display (clipping threshold)
}

// AspectRatioClass + ScreenClassification helpers live in
// libs/phosphor-layout-api as `PhosphorLayout::AspectRatioClass` /
// `PhosphorLayout::ScreenClassification`. All PlasmaZones-side callers
// reference them qualified directly — no aliases here.

/**
 * @brief XDG-relative subdirectory for scripted tiling algorithm JS files.
 *
 * Used by every in-tree @c PhosphorTiles::ScriptedAlgorithmLoader constructor
 * (daemon, editor, settings) and by the settings-side helpers that resolve
 * the user-writable directory under @c QStandardPaths::GenericDataLocation.
 * Keeping this in one place prevents the three callers from drifting when
 * the subdirectory ever needs to change.
 */
inline constexpr QLatin1String ScriptedAlgorithmSubdir{"plasmazones/algorithms"};

/**
 * @brief Editor-specific constants
 */
namespace EditorConstants {
// PhosphorZones::Zone size constraints (relative coordinates 0.0-1.0)
constexpr qreal MinZoneSize = 0.05; // 5% minimum zone size
constexpr qreal MaxZoneSize = 1.0; // 100% maximum zone size

// Fixed geometry constraints (absolute pixel coordinates)
constexpr int MinFixedZoneSize = 50; // Minimum fixed zone dimension in pixels

// Snapping thresholds (relative coordinates 0.0-1.0, used in SnappingService)
constexpr qreal EdgeThreshold = 0.02; // 2% threshold for snapping to zone edges
constexpr qreal DefaultSnapInterval = 0.1; // 10% default grid snap interval

// PhosphorZones::Zone duplication offset
constexpr qreal DuplicateOffset = 0.02; // 2% offset when duplicating zones
constexpr int DuplicateOffsetPixels = 20; // 20px offset when duplicating fixed zones

// Keyboard step for fixed geometry zones
constexpr int KeyboardStepPixels = 10; // 10px step for keyboard move/resize of fixed zones

// Floating-point tolerances (relative coordinates 0.0-1.0)
constexpr qreal OverlapThreshold = 0.002; // Epsilon for zone overlap detection
constexpr qreal CoordDedupeThreshold = 0.001; // Coordinate deduplication tolerance
constexpr qreal MinExpansionThreshold = 0.005; // Minimum expansion to accept
constexpr qreal AdjacencyThreshold = 0.02; // Default adjacency detection threshold
constexpr qreal ExpansionStep = 0.01; // 1% increment for fill expansion
constexpr qreal GeometryBoundsTolerance = 0.001; // Tolerance for coordinate bounds checking

// Default zone colors (hex strings for QML compatibility)
inline constexpr QLatin1String DefaultHighlightColor{"#800078D4"};
inline constexpr QLatin1String DefaultInactiveColor{"#40808080"};
inline constexpr QLatin1String DefaultBorderColor{"#C8FFFFFF"};
}

/**
 * @brief JSON keys for serialization
 */
namespace JsonKeys {
// PhosphorZones::Zone & layout wire-format keys live in `PhosphorZones::ZoneJsonKeys`
// (libs/phosphor-zones).  All in-tree callers reference them qualified
// directly — no using-aliases here.
//
// PlasmaZones-side JSON keys below aren't part of the zone/layout file
// format proper — assignment runtime state, screen-info enumeration,
// virtual-screen configuration, pywal colour ingestion.

// Assignment keys
inline constexpr QLatin1String Assignments{"assignments"};
inline constexpr QLatin1String ScreenId{"screenId"};
inline constexpr QLatin1String Screen{"screen"};
inline constexpr QLatin1String Desktop{"desktop"};
inline constexpr QLatin1String Activity{"activity"};
inline constexpr QLatin1String LayoutId{"layoutId"};
inline constexpr QLatin1String QuickShortcuts{"quickShortcuts"};

// Screen info keys
inline constexpr QLatin1String Geometry{"geometry"};
inline constexpr QLatin1String Manufacturer{"manufacturer"};
inline constexpr QLatin1String Model{"model"};
inline constexpr QLatin1String PhysicalSize{"physicalSize"};
inline constexpr QLatin1String Depth{"depth"};
inline constexpr QLatin1String DevicePixelRatio{"devicePixelRatio"};
inline constexpr QLatin1String RefreshRate{"refreshRate"};

// Pywal color file keys
inline constexpr QLatin1String Colors{"colors"};

// PhosphorZones::Zone assignment serialization keys
inline constexpr QLatin1String WindowId{"windowId"};
inline constexpr QLatin1String SourceZoneId{"sourceZoneId"};
inline constexpr QLatin1String TargetZoneId{"targetZoneId"};
inline constexpr QLatin1String TargetZoneIds{"targetZoneIds"};

// Virtual screen keys
inline constexpr QLatin1String IsVirtualScreen{"isVirtualScreen"};
inline constexpr QLatin1String VirtualDisplayName{"virtualDisplayName"};
inline constexpr QLatin1String PhysicalScreenId{"physicalScreenId"};
inline constexpr QLatin1String SerialNumber{"serialNumber"};
inline constexpr QLatin1String Index{"index"};
inline constexpr QLatin1String DisplayName{"displayName"};
inline constexpr QLatin1String Region{"region"};
inline constexpr QLatin1String Screens{"screens"};
}

/**
 * @brief Per-screen override key names (PascalCase — distinct from camelCase JSON keys)
 *
 * These are the keys used in PerScreenConfigResolver's override map and in
 * NavigationController's shortcut handlers. They match the key names written
 * by the per-screen config system in perscreen.cpp (with the "Autotile" prefix
 * stripped during lookup).
 */
namespace PerScreenKeys {
inline constexpr QLatin1String SplitRatio{"SplitRatio"};
inline constexpr QLatin1String SplitRatioStep{"SplitRatioStep"};
inline constexpr QLatin1String MasterCount{"MasterCount"};
inline constexpr QLatin1String Algorithm{"Algorithm"};
inline constexpr QLatin1String MaxWindows{"MaxWindows"};
inline constexpr QLatin1String InnerGap{"InnerGap"};
inline constexpr QLatin1String OuterGap{"OuterGap"};
inline constexpr QLatin1String UsePerSideOuterGap{"UsePerSideOuterGap"};
inline constexpr QLatin1String FocusNewWindows{"FocusNewWindows"};
inline constexpr QLatin1String SmartGaps{"SmartGaps"};
inline constexpr QLatin1String InsertPosition{"InsertPosition"};
inline constexpr QLatin1String FocusFollowsMouse{"FocusFollowsMouse"};
inline constexpr QLatin1String RespectMinimumSize{"RespectMinimumSize"};
inline constexpr QLatin1String HideTitleBars{"HideTitleBars"};
inline constexpr QLatin1String AnimationsEnabled{"AnimationsEnabled"};
inline constexpr QLatin1String AnimationDuration{"AnimationDuration"};
inline constexpr QLatin1String AnimationEasingCurve{"AnimationEasingCurve"};
}

/**
 * @brief Synthetic zone ID prefix used by the zone selector overlay
 *
 * PhosphorZones::Zone IDs starting with this prefix are transient selector entries,
 * not real zone UUIDs. They must be excluded from persistence and
 * occupancy checks.
 */
inline constexpr QLatin1String ZoneSelectorIdPrefix{"zoneselector-"};

/**
 * @brief Sentinel value used as targetZoneId when restoring pre-tiling geometry
 *
 * When a window is released from autotile, its zone assignment entry uses this
 * sentinel instead of a real zone UUID to signal that the window should be
 * restored to its original (pre-tiling) geometry rather than snapped to a zone.
 */
inline constexpr QLatin1StringView RestoreSentinel("__restore__");

/**
 * @brief D-Bus service constants
 *
 * ServiceName, ObjectPath, ApiVersion / MinPeerApiVersion, and the
 * per-interface name table all live in compositor-common/dbus_constants.h
 * (included at the top of this file) — that's the single source of truth
 * shared with the KWin effect, which doesn't link plasmazones_core.
 * This re-opens `namespace DBus` only to add daemon-only constants.
 */
namespace DBus {

namespace SettingsApp {
inline constexpr QLatin1String ServiceName{"org.plasmazones.Settings.App"};
inline constexpr QLatin1String ObjectPath{"/SettingsApp"};
inline constexpr QLatin1String Interface{"org.plasmazones.SettingsController"};
}

namespace EditorApp {
inline constexpr QLatin1String ServiceName{"org.plasmazones.Editor.App"};
inline constexpr QLatin1String ObjectPath{"/EditorApp"};
inline constexpr QLatin1String Interface{"org.plasmazones.EditorController"};
}

}

// LayoutId lives in libs/phosphor-layout-api as `PhosphorLayout::LayoutId`.
// All PlasmaZones-side callers reference it qualified directly — no alias
// here.

// EdgeGaps lives in libs/phosphor-layout-api as `PhosphorLayout::EdgeGaps`.
// All PlasmaZones-side callers now reference it qualified directly — no
// `using EdgeGaps = ...` alias here.

} // namespace PlasmaZones
