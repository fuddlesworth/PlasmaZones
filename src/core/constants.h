// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QLatin1String>
#include <QString>

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
// Alpha values for semi-transparent colors
constexpr int HighlightAlpha = 128;
constexpr int InactiveAlpha = 64;
constexpr int BorderAlpha = 200;
constexpr int OpaqueAlpha = 255;

// Colors
inline const QColor HighlightColor{0, 120, 212, HighlightAlpha}; // Windows blue
inline const QColor InactiveColor{128, 128, 128, InactiveAlpha};
inline const QColor BorderColor{255, 255, 255, BorderAlpha};
inline const QColor LabelFontColor{255, 255, 255, OpaqueAlpha};

// Dimensions
constexpr qreal Opacity = 0.5;
constexpr qreal InactiveOpacity = 0.3;
constexpr int BorderWidth = 2;
constexpr int BorderRadius = 8;
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

// Layout ratios
constexpr qreal PriorityGridMainRatio = 0.667;
constexpr qreal PriorityGridSecondaryRatio = 0.333;
constexpr qreal FocusSideRatio = 0.2;
constexpr qreal FocusMainRatio = 0.6;
}

/**
 * @brief Screen aspect ratio classification
 *
 * Used to tag layouts with their intended monitor type and to classify
 * physical screens at runtime for smart layout filtering/recommendations.
 */
enum class AspectRatioClass {
    Any = 0, ///< Suitable for all aspect ratios (default)
    Standard = 1, ///< ~16:10 to ~16:9 (1.5 - 1.9)
    Ultrawide = 2, ///< ~21:9 (1.9 - 2.8)
    SuperUltrawide = 3, ///< ~32:9 (2.8+)
    Portrait = 4 ///< Rotated/vertical monitors (< 1.0)
};

/**
 * @brief Screen classification thresholds and utilities
 */
namespace ScreenClassification {
// Aspect ratio boundary thresholds (width / height)
constexpr qreal PortraitMax = 1.0; // AR < 1.0 → portrait
constexpr qreal UltrawideMin = 1.9; // AR >= 1.9 and < SuperUltrawideMin → ultrawide
constexpr qreal SuperUltrawideMin = 2.8; // AR >= 2.8 → super-ultrawide

inline AspectRatioClass classify(qreal aspectRatio)
{
    if (aspectRatio < PortraitMax)
        return AspectRatioClass::Portrait;
    if (aspectRatio < UltrawideMin)
        return AspectRatioClass::Standard;
    if (aspectRatio < SuperUltrawideMin)
        return AspectRatioClass::Ultrawide;
    return AspectRatioClass::SuperUltrawide;
}

inline AspectRatioClass classify(int width, int height)
{
    if (height <= 0 || width <= 0)
        return AspectRatioClass::Any;
    return classify(static_cast<qreal>(width) / height);
}

inline QString toString(AspectRatioClass cls)
{
    switch (cls) {
    case AspectRatioClass::Any:
        return QStringLiteral("any");
    case AspectRatioClass::Standard:
        return QStringLiteral("standard");
    case AspectRatioClass::Ultrawide:
        return QStringLiteral("ultrawide");
    case AspectRatioClass::SuperUltrawide:
        return QStringLiteral("super-ultrawide");
    case AspectRatioClass::Portrait:
        return QStringLiteral("portrait");
    }
    return QStringLiteral("any");
}

inline AspectRatioClass fromString(const QString& str)
{
    if (str == QLatin1String("standard"))
        return AspectRatioClass::Standard;
    if (str == QLatin1String("ultrawide"))
        return AspectRatioClass::Ultrawide;
    if (str == QLatin1String("super-ultrawide"))
        return AspectRatioClass::SuperUltrawide;
    if (str == QLatin1String("portrait"))
        return AspectRatioClass::Portrait;
    return AspectRatioClass::Any;
}

/**
 * @brief Get the representative aspect ratio for a class
 * @param cls The aspect ratio class
 * @param fallback Value to return for AspectRatioClass::Any (default 16:9)
 * @return Representative aspect ratio (width/height)
 */
inline qreal aspectRatioForClass(AspectRatioClass cls, qreal fallback = 16.0 / 9.0)
{
    switch (cls) {
    case AspectRatioClass::Standard:
        return 16.0 / 9.0;
    case AspectRatioClass::Ultrawide:
        return 21.0 / 9.0;
    case AspectRatioClass::SuperUltrawide:
        return 32.0 / 9.0;
    case AspectRatioClass::Portrait:
        return 9.0 / 16.0;
    case AspectRatioClass::Any:
    default:
        return fallback;
    }
}

/**
 * @brief Check if a layout's aspect ratio class matches the given screen class
 *
 * A layout with AspectRatioClass::Any matches all screens.
 * Otherwise, exact match is required unless the layout specifies
 * explicit min/max aspect ratio bounds (which take precedence).
 */
inline bool matches(AspectRatioClass layoutClass, AspectRatioClass screenClass)
{
    if (layoutClass == AspectRatioClass::Any)
        return true;
    return layoutClass == screenClass;
}
}

/**
 * @brief Auto-tiling algorithm defaults
 */
namespace AutotileDefaults {
// Min/max constraints — default values live in ConfigDefaults (single source of truth)
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
constexpr int MaxZones = 256;
constexpr int MaxRuntimeTreeDepth = 50; ///< Maximum recursion depth for split tree operations
constexpr qreal SplitRatioHysteresis = 0.05; ///< Band within which algorithm-switch ratio reset is suppressed
constexpr int MinMetadataWindows = 1;
constexpr int MaxMetadataWindows = 100;
constexpr int MinInsertPosition = 0;
constexpr int MaxInsertPosition = 2;
constexpr int MinAnimationDuration = 50;
constexpr int MaxAnimationDuration = 500;
constexpr int MinAnimationStaggerIntervalMs = 10;
constexpr int MaxAnimationStaggerIntervalMs = 200;

/// Returns true if typeId is a numeric QMetaType (Double, Float, Int, UInt, LongLong, ULongLong).
/// Used for fuzzy-comparing QVariant values after JSON round-trip type drift.
constexpr bool isNumericMetaType(int typeId)
{
    return typeId == QMetaType::Double || typeId == QMetaType::Float || typeId == QMetaType::Int
        || typeId == QMetaType::UInt || typeId == QMetaType::LongLong || typeId == QMetaType::ULongLong;
}
}

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
inline constexpr QLatin1String CustomParams{"customParams"};

// AutotileConfig keys
inline constexpr QLatin1String PerAlgorithmSettings{"perAlgorithmSettings"};
inline constexpr QLatin1String AlgorithmId{"algorithmId"};
inline constexpr QLatin1String InnerGap{"innerGap"};
inline constexpr QLatin1String OuterGap{"outerGap"};
// Per-side outer gap keys (shared with LayoutJsonKeys — same wire format)
using JsonKeys::OuterGapBottom;
using JsonKeys::OuterGapLeft;
using JsonKeys::OuterGapRight;
using JsonKeys::OuterGapTop;
using JsonKeys::UsePerSideOuterGap;
inline constexpr QLatin1String SmartGaps{"smartGaps"};
inline constexpr QLatin1String FocusNewWindows{"focusNewWindows"};
inline constexpr QLatin1String FocusFollowsMouse{"focusFollowsMouse"};
inline constexpr QLatin1String InsertPosition{"insertPosition"};
inline constexpr QLatin1String RespectMinimumSize{"respectMinimumSize"};
inline constexpr QLatin1String MaxWindows{"maxWindows"};
inline constexpr QLatin1String CenteredMasterSplitRatio{"centeredMasterSplitRatio"};
inline constexpr QLatin1String CenteredMasterMasterCount{"centeredMasterMasterCount"};
// InsertPosition values
inline constexpr QLatin1String InsertEnd{"end"};
inline constexpr QLatin1String InsertAfterFocused{"afterFocused"};
inline constexpr QLatin1String InsertAsMaster{"asMaster"};
inline constexpr QLatin1String SplitTreeKey{"splitTree"};
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
inline const QString RestoreSentinel = QStringLiteral("__restore__");

/**
 * @brief D-Bus service constants
 */
namespace DBus {
inline constexpr QLatin1String ServiceName{"org.plasmazones"};
inline constexpr QLatin1String ObjectPath{"/PlasmaZones"};

namespace SettingsApp {
inline constexpr QLatin1String ServiceName{"org.plasmazones.Settings.App"};
inline constexpr QLatin1String ObjectPath{"/SettingsApp"};
inline constexpr QLatin1String Interface{"org.plasmazones.SettingsController"};
}

namespace Interface {
inline constexpr QLatin1String LayoutManager{"org.plasmazones.LayoutManager"};
inline constexpr QLatin1String Overlay{"org.plasmazones.Overlay"};
inline constexpr QLatin1String Settings{"org.plasmazones.Settings"};
inline constexpr QLatin1String Screen{"org.plasmazones.Screen"};
inline constexpr QLatin1String WindowDrag{"org.plasmazones.WindowDrag"};
inline constexpr QLatin1String WindowTracking{"org.plasmazones.WindowTracking"};
inline constexpr QLatin1String ZoneDetection{"org.plasmazones.ZoneDetection"};
inline constexpr QLatin1String Autotile{"org.plasmazones.Autotile"};
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

/**
 * @brief Per-side edge gap values (resolved, non-negative pixel values)
 *
 * Used when usePerSideOuterGap is enabled to apply different gaps
 * to each screen edge. When disabled, the single outerGap value
 * is used uniformly via EdgeGaps::uniform().
 *
 * Default member values (8px) represent the application default.
 * Note: Layout::rawOuterGaps() returns an EdgeGaps with -1 sentinels
 * (meaning "use global setting") — those must be resolved via
 * getEffectiveOuterGaps() before use in geometry calculations.
 */
struct EdgeGaps
{
    int top = 8;
    int bottom = 8;
    int left = 8;
    int right = 8;
    bool operator==(const EdgeGaps&) const = default;
    bool isUniform() const
    {
        return top == bottom && bottom == left && left == right;
    }
    static EdgeGaps uniform(int gap)
    {
        return {gap, gap, gap, gap};
    }
};

} // namespace PlasmaZones
