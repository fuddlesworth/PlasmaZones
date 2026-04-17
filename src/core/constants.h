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
#include <PhosphorZones/ZoneJsonKeys.h>

// NOTE: AutotileJsonKeys / AutotileDefaults previously lived inline here and
// were re-exported from <PhosphorTiles/AutotileConstants.h> via a transitive
// include.  That transitive chain forced every PlasmaZones consumer (including
// libraries like PhosphorZones that have no use for autotile symbols) to
// resolve the PhosphorTiles include path.  Consumers that genuinely need the
// autotile namespaces now include `<PhosphorTiles/AutotileConstants.h>` (or
// the legacy shim `autotile/AutotileConstants.h`) directly.

namespace PlasmaZones {

// ZoneGeometryMode lives in libs/phosphor-zones — `PhosphorZones::Zone.h`
// declares it inside `namespace PlasmaZones` so it's visible to existing
// callers via the same name.  No alias needed here — the header transitively
// reaches Zone.h via core/zone.h shim.

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
using ::PhosphorZones::ZoneDefaults::AdjacentThreshold;
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
// Zone & layout wire-format keys live in libs/phosphor-zones —
// `PhosphorZones::ZoneJsonKeys`.  Re-exported here so existing
// PlasmaZones::JsonKeys::* callers compile unchanged.
using ::PhosphorZones::ZoneJsonKeys::Appearance;
using ::PhosphorZones::ZoneJsonKeys::Id;
using ::PhosphorZones::ZoneJsonKeys::Name;
using ::PhosphorZones::ZoneJsonKeys::RelativeGeometry;
using ::PhosphorZones::ZoneJsonKeys::ZoneId;
using ::PhosphorZones::ZoneJsonKeys::ZoneNumber;

using ::PhosphorZones::ZoneJsonKeys::Height;
using ::PhosphorZones::ZoneJsonKeys::Width;
using ::PhosphorZones::ZoneJsonKeys::X;
using ::PhosphorZones::ZoneJsonKeys::Y;
using ::PhosphorZones::ZoneJsonKeys::ZOrder;

using ::PhosphorZones::ZoneJsonKeys::ActiveOpacity;
using ::PhosphorZones::ZoneJsonKeys::BorderColor;
using ::PhosphorZones::ZoneJsonKeys::BorderRadius;
using ::PhosphorZones::ZoneJsonKeys::BorderWidth;
using ::PhosphorZones::ZoneJsonKeys::HighlightColor;
using ::PhosphorZones::ZoneJsonKeys::InactiveColor;
using ::PhosphorZones::ZoneJsonKeys::InactiveOpacity;
using ::PhosphorZones::ZoneJsonKeys::IsHighlighted;
using ::PhosphorZones::ZoneJsonKeys::UseCustomColors;

using ::PhosphorZones::ZoneJsonKeys::Category;
using ::PhosphorZones::ZoneJsonKeys::DefaultOrder;
using ::PhosphorZones::ZoneJsonKeys::Description;
using ::PhosphorZones::ZoneJsonKeys::HasSystemOrigin;
using ::PhosphorZones::ZoneJsonKeys::IsBuiltIn;
using ::PhosphorZones::ZoneJsonKeys::IsSystem;
using ::PhosphorZones::ZoneJsonKeys::OuterGap;
using ::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode;
using ::PhosphorZones::ZoneJsonKeys::ShowZoneNumbers;
using ::PhosphorZones::ZoneJsonKeys::SystemSourcePath;
using ::PhosphorZones::ZoneJsonKeys::ZoneCount;
using ::PhosphorZones::ZoneJsonKeys::ZonePadding;
using ::PhosphorZones::ZoneJsonKeys::Zones;

using ::PhosphorZones::ZoneJsonKeys::ShaderId;
using ::PhosphorZones::ZoneJsonKeys::ShaderParams;

using ::PhosphorZones::ZoneJsonKeys::AllowedActivities;
using ::PhosphorZones::ZoneJsonKeys::AllowedDesktops;
using ::PhosphorZones::ZoneJsonKeys::AllowedScreens;
using ::PhosphorZones::ZoneJsonKeys::HiddenFromSelector;

using ::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey;
using ::PhosphorZones::ZoneJsonKeys::MaxAspectRatio;
using ::PhosphorZones::ZoneJsonKeys::MinAspectRatio;

using ::PhosphorZones::ZoneJsonKeys::AppRules;
using ::PhosphorZones::ZoneJsonKeys::AutoAssign;
using ::PhosphorZones::ZoneJsonKeys::Pattern;
using ::PhosphorZones::ZoneJsonKeys::TargetScreen;

using ::PhosphorZones::ZoneJsonKeys::FixedGeometry;
using ::PhosphorZones::ZoneJsonKeys::FixedHeight;
using ::PhosphorZones::ZoneJsonKeys::FixedWidth;
using ::PhosphorZones::ZoneJsonKeys::FixedX;
using ::PhosphorZones::ZoneJsonKeys::FixedY;
using ::PhosphorZones::ZoneJsonKeys::GeometryMode;
using ::PhosphorZones::ZoneJsonKeys::UseFullScreenGeometry;

using ::PhosphorZones::ZoneJsonKeys::OuterGapBottom;
using ::PhosphorZones::ZoneJsonKeys::OuterGapLeft;
using ::PhosphorZones::ZoneJsonKeys::OuterGapRight;
using ::PhosphorZones::ZoneJsonKeys::OuterGapTop;
using ::PhosphorZones::ZoneJsonKeys::UsePerSideOuterGap;

// PlasmaZones-side JSON keys that aren't part of the zone/layout file
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
