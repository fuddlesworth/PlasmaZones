// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

/**
 * @brief Sticky window handling options
 */
enum class StickyWindowHandling {
    TreatAsNormal = 0, ///< Sticky windows follow per-desktop behavior
    RestoreOnly = 1, ///< Allow restore, disable auto-snap
    IgnoreAll = 2 ///< Disable restore and auto-snap
};

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

/**
 * @brief Behavior when a user drags a tiled window on an autotile screen.
 *
 * Float (default, PlasmaZones-native): dragging a tile converts it to a free-floating
 * window that restores its pre-tile geometry. Matches snap-mode semantics.
 *
 * Reorder (dwm/Krohnkite-style): dragging a tile keeps it tiled; the daemon tracks the
 * cursor over calculated zones and, on drop, reorders the window to the target slot.
 * Empty-space drops snap back to the original position.
 */
enum class AutotileDragBehavior {
    Float = 0, ///< Drag-to-float: converts dragged tile to floating (default)
    Reorder = 1 ///< Drag-to-reorder: swaps tile position within the layout
};

/**
 * @brief Behavior when more windows exist than the algorithm's `maxWindows` cap.
 *
 * Float (default, PlasmaZones-native): excess windows are auto-floated by
 * OverflowManager so the layout stays within the cap. New windows past the
 * cap are rejected at onWindowAdded. The cap is algorithm-dependent and
 * defaults to 4–6 on most bundled algorithms.
 *
 * Unlimited (dwm/Krohnkite-style): the cap is ignored. All windows on an
 * autotile screen are tiled regardless of count. The layout algorithm handles
 * arbitrary N — master-stack's stack area just keeps subdividing. Users who
 * dislike very thin stack slices can still set masterCount or switch layouts.
 */
enum class AutotileOverflowBehavior {
    Float = 0, ///< Float windows past the maxWindows cap (default)
    Unlimited = 1 ///< Ignore the cap entirely — every window tiles
};

} // namespace PlasmaZones
