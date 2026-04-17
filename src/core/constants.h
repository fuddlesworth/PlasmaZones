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
#include <PhosphorZones/ZoneDefaults.h>

// NOTE: AutotileJsonKeys / AutotileDefaults previously lived inline here and
// were re-exported from <PhosphorTiles/AutotileConstants.h> via a transitive
// include.  That transitive chain forced every PlasmaZones consumer (including
// libraries like PhosphorZones that have no use for autotile symbols) to
// resolve the PhosphorTiles include path.  Consumers that genuinely need the
// autotile namespaces now include `<PhosphorTiles/AutotileConstants.h>` (or
// the legacy shim `autotile/AutotileConstants.h`) directly.

namespace PlasmaZones {

/**
 * @brief Geometry mode for individual zones
 *
 * Relative: 0.0-1.0 normalized coordinates (default, resolution-independent)
 * Fixed: Absolute pixel coordinates relative to reference screen origin
 */
enum class ZoneGeometryMode {
    Relative = 0,
    Fixed = 1
};

/**
 * @brief Default values for zone appearance and core module constants
 *
 * These defaults are used by core module files that can't depend on config.
 * For user-configurable settings, see ConfigDefaults.
 *
 * Layout ratio constants (PriorityGridMainRatio, FocusSideRatio, etc.) are
 * structural constants for built-in layouts.
 */
namespace Defaults {
// Fallback screen dimensions when no QScreen is available
inline constexpr int FallbackScreenWidth = 1920;
inline constexpr int FallbackScreenHeight = 1080;

// Zone-presentation defaults (alpha + colors + appearance) live in
// libs/phosphor-zones — `PhosphorZones::ZoneDefaults`.  Re-exported here so
// existing PlasmaZones::Defaults::HighlightColor / Opacity / BorderWidth
// callers compile unchanged.  The application config layer's user-facing
// accessors (ConfigDefaults::*) continue to delegate downward.
using ::PhosphorZones::ZoneDefaults::BorderAlpha;
using ::PhosphorZones::ZoneDefaults::BorderColor;
using ::PhosphorZones::ZoneDefaults::BorderRadius;
using ::PhosphorZones::ZoneDefaults::BorderWidth;
using ::PhosphorZones::ZoneDefaults::HighlightAlpha;
using ::PhosphorZones::ZoneDefaults::HighlightColor;
using ::PhosphorZones::ZoneDefaults::InactiveAlpha;
using ::PhosphorZones::ZoneDefaults::InactiveColor;
using ::PhosphorZones::ZoneDefaults::InactiveOpacity;
using ::PhosphorZones::ZoneDefaults::LabelFontColor;
using ::PhosphorZones::ZoneDefaults::Opacity;
using ::PhosphorZones::ZoneDefaults::OpaqueAlpha;
// Layout-factory split ratios — also library-owned (PhosphorZones).
using ::PhosphorZones::ZoneDefaults::FocusMainRatio;
using ::PhosphorZones::ZoneDefaults::FocusSideRatio;
using ::PhosphorZones::ZoneDefaults::PriorityGridMainRatio;
using ::PhosphorZones::ZoneDefaults::PriorityGridSecondaryRatio;

// PlasmaZones-side defaults that aren't part of the zone-presentation
// surface — daemon overlay / settings / geometry constants.
constexpr int ZonePadding = 8;
constexpr int OuterGap = 8; // Gap at screen edges (separate from zonePadding between zones)
constexpr int MaxGap = 50; // Maximum for zone padding and outer gap settings
constexpr int AdjacentThreshold = 20;
// EdgeThreshold for overlay window detection (pixels, used in WindowTracker/Overlay)
constexpr qreal EdgeThreshold = 15.0;

// Performance and behavior constants (configurable via Settings)
constexpr int PollIntervalMs = 50; // Window move detection polling interval (20 FPS)
constexpr int MinimumZoneSizePx = 100; // Minimum zone size for window snapping
constexpr int MinimumZoneDisplaySizePx = 10; // Minimum zone size for display (clipping threshold)
}

// AspectRatioClass + ScreenClassification helpers live in
// libs/phosphor-layout-api so every layout/tile provider can speak the same
// classification vocabulary.  Aliased here so existing PlasmaZones::
// AspectRatioClass / ScreenClassification:: callers compile unchanged.
using AspectRatioClass = ::PhosphorLayout::AspectRatioClass;
namespace ScreenClassification = ::PhosphorLayout::ScreenClassification;

// AutotileDefaults lives in autotile/AutotileConstants.h (re-included above).

/**
 * @brief Editor-specific constants
 */
namespace EditorConstants {
// Zone size constraints (relative coordinates 0.0-1.0)
constexpr qreal MinZoneSize = 0.05; // 5% minimum zone size
constexpr qreal MaxZoneSize = 1.0; // 100% maximum zone size

// Fixed geometry constraints (absolute pixel coordinates)
constexpr int MinFixedZoneSize = 50; // Minimum fixed zone dimension in pixels

// Snapping thresholds (relative coordinates 0.0-1.0, used in SnappingService)
constexpr qreal EdgeThreshold = 0.02; // 2% threshold for snapping to zone edges
constexpr qreal DefaultSnapInterval = 0.1; // 10% default grid snap interval

// Zone duplication offset
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
// Zone keys
inline constexpr QLatin1String Id{"id"};
inline constexpr QLatin1String ZoneId{"zoneId"};
inline constexpr QLatin1String Name{"name"};
inline constexpr QLatin1String ZoneNumber{"zoneNumber"};
inline constexpr QLatin1String RelativeGeometry{"relativeGeometry"};
inline constexpr QLatin1String Appearance{"appearance"};

// Geometry keys
inline constexpr QLatin1String X{"x"};
inline constexpr QLatin1String Y{"y"};
inline constexpr QLatin1String Width{"width"};
inline constexpr QLatin1String Height{"height"};
inline constexpr QLatin1String ZOrder{"zOrder"};

// Appearance keys
inline constexpr QLatin1String HighlightColor{"highlightColor"};
inline constexpr QLatin1String InactiveColor{"inactiveColor"};
inline constexpr QLatin1String BorderColor{"borderColor"};
inline constexpr QLatin1String ActiveOpacity{"activeOpacity"};
inline constexpr QLatin1String InactiveOpacity{"inactiveOpacity"};
inline constexpr QLatin1String BorderWidth{"borderWidth"};
inline constexpr QLatin1String BorderRadius{"borderRadius"};
inline constexpr QLatin1String UseCustomColors{"useCustomColors"};
inline constexpr QLatin1String IsHighlighted{"isHighlighted"};

// Layout keys
inline constexpr QLatin1String DefaultOrder{"defaultOrder"};
inline constexpr QLatin1String Description{"description"};
inline constexpr QLatin1String Zones{"zones"};
inline constexpr QLatin1String ZonePadding{"zonePadding"};
inline constexpr QLatin1String OuterGap{"outerGap"};
inline constexpr QLatin1String ShowZoneNumbers{"showZoneNumbers"};
inline constexpr QLatin1String OverlayDisplayMode{"overlayDisplayMode"};
inline constexpr QLatin1String IsBuiltIn{"isBuiltIn"}; // Legacy, for backward compat when loading
inline constexpr QLatin1String IsSystem{"isSystem"}; // New: determined by source path
inline constexpr QLatin1String HasSystemOrigin{"hasSystemOrigin"}; // True if user override of a system layout
inline constexpr QLatin1String SystemSourcePath{"systemSourcePath"}; // Original system layout path (for user overrides)
inline constexpr QLatin1String ZoneCount{"zoneCount"};
inline constexpr QLatin1String Category{"category"}; // LayoutCategory: 0=Manual, 1=Autotile

// Shader keys
inline constexpr QLatin1String ShaderId{"shaderId"};
inline constexpr QLatin1String ShaderParams{"shaderParams"};

// Visibility filtering keys
inline constexpr QLatin1String HiddenFromSelector{"hiddenFromSelector"};
inline constexpr QLatin1String AllowedScreens{"allowedScreens"};
inline constexpr QLatin1String AllowedDesktops{"allowedDesktops"};
inline constexpr QLatin1String AllowedActivities{"allowedActivities"};

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

// Aspect ratio classification keys
inline constexpr QLatin1String AspectRatioClassKey{"aspectRatioClass"};
inline constexpr QLatin1String MinAspectRatio{"minAspectRatio"};
inline constexpr QLatin1String MaxAspectRatio{"maxAspectRatio"};

// App rules keys
inline constexpr QLatin1String AppRules{"appRules"};
inline constexpr QLatin1String Pattern{"pattern"};
// ZoneNumber is reused from zone keys above
inline constexpr QLatin1String TargetScreen{"targetScreen"};

// Auto-assign keys
inline constexpr QLatin1String AutoAssign{"autoAssign"};

// Geometry mode keys
inline constexpr QLatin1String UseFullScreenGeometry{"useFullScreenGeometry"};

// Per-zone geometry mode keys
inline constexpr QLatin1String GeometryMode{"geometryMode"};
inline constexpr QLatin1String FixedGeometry{"fixedGeometry"};
inline constexpr QLatin1String FixedX{"fixedX"};
inline constexpr QLatin1String FixedY{"fixedY"};
inline constexpr QLatin1String FixedWidth{"fixedWidth"};
inline constexpr QLatin1String FixedHeight{"fixedHeight"};

// Per-side outer gap keys
inline constexpr QLatin1String UsePerSideOuterGap{"usePerSideOuterGap"};
inline constexpr QLatin1String OuterGapTop{"outerGapTop"};
inline constexpr QLatin1String OuterGapBottom{"outerGapBottom"};
inline constexpr QLatin1String OuterGapLeft{"outerGapLeft"};
inline constexpr QLatin1String OuterGapRight{"outerGapRight"};

// Pywal color file keys
inline constexpr QLatin1String Colors{"colors"};

// Zone assignment serialization keys
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

// AutotileJsonKeys lives in autotile/AutotileConstants.h (re-included above).

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
 * @brief Audio visualization constants (CAVA)
 */
namespace Audio {
constexpr int MinBars = 16;
constexpr int MaxBars = 256;
}

/**
 * @brief Synthetic zone ID prefix used by the zone selector overlay
 *
 * Zone IDs starting with this prefix are transient selector entries,
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

/**
 * @brief Layout ID utilities for autotile algorithm layouts
 *
 * Autotile layouts use prefixed IDs: "autotile:algorithm-id"
 * Manual layouts use UUID strings.
 */
namespace LayoutId {
inline constexpr QLatin1String AutotilePrefix{"autotile:"};

inline bool isAutotile(const QString& id)
{
    return id.startsWith(AutotilePrefix);
}

inline QString extractAlgorithmId(const QString& id)
{
    if (!isAutotile(id))
        return QString();
    return id.mid(AutotilePrefix.size());
}

inline QString makeAutotileId(const QString& algorithmId)
{
    return AutotilePrefix + algorithmId;
}
}

// EdgeGaps now lives in PhosphorLayoutApi (shared between manual layout and
// tiling).  Aliased here so existing PlasmaZones::EdgeGaps callers compile
// without changes.
using EdgeGaps = ::PhosphorLayout::EdgeGaps;

} // namespace PlasmaZones
