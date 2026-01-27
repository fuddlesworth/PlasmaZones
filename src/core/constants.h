// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QLatin1String>

namespace PlasmaZones {

/**
 * @brief Default values for zone appearance
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
inline const QColor NumberColor{255, 255, 255, OpaqueAlpha};

// Dimensions
constexpr qreal Opacity = 0.5;
constexpr qreal InactiveOpacity = 0.3;
constexpr int BorderWidth = 2;
constexpr int BorderRadius = 8;
constexpr int ZonePadding = 8;
constexpr int OuterGap = 8;  // Gap at screen edges (separate from zonePadding between zones)
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
 * @brief Editor-specific constants
 */
namespace EditorConstants {
// Zone size constraints (relative coordinates 0.0-1.0)
constexpr qreal MinZoneSize = 0.05; // 5% minimum zone size
constexpr qreal MaxZoneSize = 1.0; // 100% maximum zone size

// Snapping thresholds (relative coordinates 0.0-1.0, used in SnappingService)
constexpr qreal EdgeThreshold = 0.02; // 2% threshold for snapping to zone edges
constexpr qreal DefaultSnapInterval = 0.1; // 10% default grid snap interval

// Zone duplication offset
constexpr qreal DuplicateOffset = 0.02; // 2% offset when duplicating zones

// Default zone colors (hex strings for QML compatibility)
inline constexpr const char* DefaultHighlightColor = "#800078D4";
inline constexpr const char* DefaultInactiveColor = "#40808080";
inline constexpr const char* DefaultBorderColor = "#CCFFFFFF";
}

/**
 * @brief JSON keys for serialization (DRY principle)
 */
namespace JsonKeys {
// Zone keys
inline constexpr QLatin1String Id{"id"};
inline constexpr QLatin1String Name{"name"};
inline constexpr QLatin1String ZoneNumber{"zoneNumber"};
inline constexpr QLatin1String Shortcut{"shortcut"};
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
inline constexpr QLatin1String Type{"type"};
inline constexpr QLatin1String Description{"description"};
inline constexpr QLatin1String Author{"author"};
inline constexpr QLatin1String Zones{"zones"};
inline constexpr QLatin1String ZonePadding{"zonePadding"};
inline constexpr QLatin1String OuterGap{"outerGap"};
inline constexpr QLatin1String ShowZoneNumbers{"showZoneNumbers"};
inline constexpr QLatin1String IsBuiltIn{"isBuiltIn"}; // Legacy, for backward compat when loading
inline constexpr QLatin1String IsSystem{"isSystem"}; // New: determined by source path
inline constexpr QLatin1String ZoneCount{"zoneCount"};

// Shader keys
inline constexpr QLatin1String ShaderId{"shaderId"};
inline constexpr QLatin1String ShaderParams{"shaderParams"};

// Assignment keys
inline constexpr QLatin1String Assignments{"assignments"};
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
}

/**
 * @brief D-Bus service constants
 */
namespace DBus {
inline constexpr QLatin1String ServiceName{"org.plasmazones"};
inline constexpr QLatin1String ObjectPath{"/PlasmaZones"};

namespace Interface {
inline constexpr QLatin1String LayoutManager{"org.plasmazones.LayoutManager"};
inline constexpr QLatin1String Overlay{"org.plasmazones.Overlay"};
inline constexpr QLatin1String Settings{"org.plasmazones.Settings"};
inline constexpr QLatin1String Screen{"org.plasmazones.Screen"};
inline constexpr QLatin1String ScreenManager{"org.plasmazones.ScreenManager"};
inline constexpr QLatin1String WindowDrag{"org.plasmazones.WindowDrag"};
inline constexpr QLatin1String WindowTracking{"org.plasmazones.WindowTracking"};
inline constexpr QLatin1String ZoneDetection{"org.plasmazones.ZoneDetection"};
}
}

} // namespace PlasmaZones
