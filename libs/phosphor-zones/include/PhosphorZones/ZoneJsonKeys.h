// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1String>

namespace PhosphorZones {

/**
 * @brief JSON wire-format keys for zone & layout serialisation.
 *
 * The zone/layout file format is part of phosphor-zones' contract — any
 * other library that reads or writes a layout file must spell these keys
 * the same way.  Owning them here keeps the wire format under the
 * library's source control rather than scattered across the application.
 *
 * Keys that aren't part of the zone/layout file format proper (window-
 * assignment runtime state, screen-info enumeration, virtual-screen
 * configuration, pywal colour ingestion, autotile JSON) live in their
 * respective owners — see PlasmaZones::JsonKeys (src/core/constants.h)
 * and PhosphorTiles::AutotileJsonKeys (libs/phosphor-tiles).
 */
namespace ZoneJsonKeys {

// Zone-identity keys.
inline constexpr QLatin1String Id{"id"};
inline constexpr QLatin1String ZoneId{"zoneId"};
inline constexpr QLatin1String Name{"name"};
inline constexpr QLatin1String ZoneNumber{"zoneNumber"};
inline constexpr QLatin1String RelativeGeometry{"relativeGeometry"};
inline constexpr QLatin1String Appearance{"appearance"};

// Per-zone geometry keys.
inline constexpr QLatin1String X{"x"};
inline constexpr QLatin1String Y{"y"};
inline constexpr QLatin1String Width{"width"};
inline constexpr QLatin1String Height{"height"};
inline constexpr QLatin1String ZOrder{"zOrder"};

// Per-zone appearance keys.
inline constexpr QLatin1String HighlightColor{"highlightColor"};
inline constexpr QLatin1String InactiveColor{"inactiveColor"};
inline constexpr QLatin1String BorderColor{"borderColor"};
inline constexpr QLatin1String ActiveOpacity{"activeOpacity"};
inline constexpr QLatin1String InactiveOpacity{"inactiveOpacity"};
inline constexpr QLatin1String BorderWidth{"borderWidth"};
inline constexpr QLatin1String BorderRadius{"borderRadius"};
inline constexpr QLatin1String UseCustomColors{"useCustomColors"};
inline constexpr QLatin1String IsHighlighted{"isHighlighted"};

// Layout-level keys.
inline constexpr QLatin1String DefaultOrder{"defaultOrder"};
inline constexpr QLatin1String Description{"description"};
inline constexpr QLatin1String Zones{"zones"};
inline constexpr QLatin1String ZonePadding{"zonePadding"};
inline constexpr QLatin1String OuterGap{"outerGap"};
inline constexpr QLatin1String ShowZoneNumbers{"showZoneNumbers"};
inline constexpr QLatin1String OverlayDisplayMode{"overlayDisplayMode"};
inline constexpr QLatin1String IsBuiltIn{"isBuiltIn"}; ///< Legacy, for backward-compat when loading
inline constexpr QLatin1String IsSystem{"isSystem"}; ///< New: determined by source path
inline constexpr QLatin1String HasSystemOrigin{"hasSystemOrigin"}; ///< User override of a system layout
inline constexpr QLatin1String SystemSourcePath{"systemSourcePath"}; ///< Original system layout path
inline constexpr QLatin1String ZoneCount{"zoneCount"};
inline constexpr QLatin1String Category{"category"}; ///< 0=Manual, 1=Autotile

// Shader-binding keys (per-zone shader pipeline overrides).
inline constexpr QLatin1String ShaderId{"shaderId"};
inline constexpr QLatin1String ShaderParams{"shaderParams"};

// Visibility-filtering keys (which screens / desktops / activities
// the layout is offered on).
inline constexpr QLatin1String HiddenFromSelector{"hiddenFromSelector"};
inline constexpr QLatin1String AllowedScreens{"allowedScreens"};
inline constexpr QLatin1String AllowedDesktops{"allowedDesktops"};
inline constexpr QLatin1String AllowedActivities{"allowedActivities"};

// Aspect-ratio classification keys.
inline constexpr QLatin1String AspectRatioClassKey{"aspectRatioClass"};
inline constexpr QLatin1String MinAspectRatio{"minAspectRatio"};
inline constexpr QLatin1String MaxAspectRatio{"maxAspectRatio"};

// App-rule (auto-assign) keys.
inline constexpr QLatin1String AppRules{"appRules"};
inline constexpr QLatin1String Pattern{"pattern"};
// `ZoneNumber` (above) is reused for the rule target.
inline constexpr QLatin1String TargetScreen{"targetScreen"};
inline constexpr QLatin1String AutoAssign{"autoAssign"};

// Geometry-mode keys.
inline constexpr QLatin1String UseFullScreenGeometry{"useFullScreenGeometry"};
inline constexpr QLatin1String GeometryMode{"geometryMode"};
inline constexpr QLatin1String FixedGeometry{"fixedGeometry"};
inline constexpr QLatin1String FixedX{"fixedX"};
inline constexpr QLatin1String FixedY{"fixedY"};
inline constexpr QLatin1String FixedWidth{"fixedWidth"};
inline constexpr QLatin1String FixedHeight{"fixedHeight"};

// Per-side outer-gap keys.
inline constexpr QLatin1String UsePerSideOuterGap{"usePerSideOuterGap"};
inline constexpr QLatin1String OuterGapTop{"outerGapTop"};
inline constexpr QLatin1String OuterGapBottom{"outerGapBottom"};
inline constexpr QLatin1String OuterGapLeft{"outerGapLeft"};
inline constexpr QLatin1String OuterGapRight{"outerGapRight"};

} // namespace ZoneJsonKeys

} // namespace PhosphorZones
