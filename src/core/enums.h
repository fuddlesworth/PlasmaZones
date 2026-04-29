// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorEngineApi/EngineTypes.h>
#include <PhosphorTiles/AutotileConstants.h>

namespace PlasmaZones {

/**
 * @brief Keyboard modifier options for drag activation
 *
 * On Wayland, modifier detection may not work reliably because background daemons
 * can't query global keyboard state. If modifiers aren't detected, use AlwaysActive
 * as a workaround.
 */
enum class DragModifier {
    Disabled = 0, ///< Disabled - zone overlay never shows on drag
    Shift = 1, ///< Hold Shift while dragging
    Ctrl = 2, ///< Hold Ctrl while dragging
    Alt = 3, ///< Hold Alt while dragging
    Meta = 4, ///< Hold Meta/Super while dragging
    CtrlAlt = 5, ///< Hold Ctrl+Alt while dragging
    CtrlShift = 6, ///< Hold Ctrl+Shift while dragging
    AltShift = 7, ///< Hold Alt+Shift while dragging
    AlwaysActive = 8, ///< Always show zones on any drag (no modifier needed)
    AltMeta = 9, ///< Hold Alt+Meta while dragging
    CtrlAltMeta = 10 ///< Hold Ctrl+Alt+Meta while dragging
};

/**
 * @brief Position options for the zone selector bar
 * Values correspond to 3x3 grid cell indices:
 *   0=TopLeft,    1=Top,    2=TopRight
 *   3=Left,       4=Center, 5=Right
 *   6=BottomLeft, 7=Bottom, 8=BottomRight
 */
enum class ZoneSelectorPosition {
    TopLeft = 0,
    Top = 1,
    TopRight = 2,
    Left = 3,
    Center = 4,
    Right = 5,
    BottomLeft = 6,
    Bottom = 7,
    BottomRight = 8
};

/**
 * @brief PhosphorZones::Layout mode options for the zone selector
 */
enum class ZoneSelectorLayoutMode {
    Grid = 0, ///< Grid layout with configurable columns
    Horizontal = 1, ///< Single row layout
    Vertical = 2 ///< Single column layout
};

/**
 * @brief Size mode options for the zone selector
 */
enum class ZoneSelectorSizeMode {
    Auto = 0, ///< Auto-calculate preview size from screen dimensions and layout count
    Manual = 1 ///< Use explicit previewWidth/previewHeight settings
};

using StickyWindowHandling = PhosphorEngineApi::StickyWindowHandling;

/**
 * @brief OSD style options for layout switch notifications
 */
enum class OsdStyle {
    None = 0, ///< No OSD shown on layout switch
    Text = 1, ///< Text-only Plasma OSD (layout name)
    Preview = 2 ///< Visual layout preview OSD (default)
};

/**
 * @brief Overlay display mode options
 *
 * Controls how zones are rendered during drag overlay:
 * - ZoneRectangles: Full-size translucent rectangles at actual zone positions (default)
 * - LayoutPreview: Small layout preview thumbnail centered in each zone (kZones-style)
 */
enum class OverlayDisplayMode {
    ZoneRectangles = 0, ///< Current behavior: full-size translucent rectangles
    LayoutPreview = 1 ///< kZones-style: small layout thumbnail per zone
};

using AutotileDragBehavior = PhosphorTiles::AutotileDragBehavior;
using AutotileOverflowBehavior = PhosphorTiles::AutotileOverflowBehavior;

} // namespace PlasmaZones
