// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Drop-target indicator for a scrolling drag re-insert.
 *
 * Outlines the slot the dragged window would land in if it were dropped now.
 * The rect arrives from C++ as `indicatorRect` in shell-window coordinates,
 * already resolved by the scroll engine against real layout math, so this
 * component does no geometry of its own. Updates are plain property writes;
 * the component is not re-instantiated as the target moves.
 *
 * Scrolling needs this drawn where autotile needs nothing: autotile's feedback
 * IS its live restructure, but the scroll engine detaches once at drag start
 * and applies structure at drop, so nothing in the strip moves to show where
 * the window is going.
 *
 * The rect is NOT clamped to the viewport. A target that opens a column past
 * the visible edge genuinely lies off screen, and clamping would draw a
 * plausible-looking indicator over the wrong slot.
 *
 * INPUT: none. No pointer handlers, and the daemon contributes no input region
 * for this slot, because it is painted underneath a cursor that is mid-drag.
 */

import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: root

    /// Drop-target rect in shell-window coordinates.
    required property rect indicatorRect
    /// Configured fill and border colours, always CONCRETE. The
    /// follow-the-theme sentinel is resolved before it gets here, in Settings
    /// (resolvedSystemColor), so this component holds no fallback rule of its
    /// own and cannot disagree with the swatch the settings page previews.
    required property color indicatorColor
    required property color indicatorBorderColor
    /// Fill opacity. Applies to the fill only; the border's transparency
    /// comes from its own colour's alpha channel.
    required property real indicatorOpacity
    required property int indicatorBorderWidth
    required property int indicatorBorderRadius
    /// Whether a rect change should be ANIMATED. True for a cursor-driven
    /// target change, which is what the transitions exist to make legible.
    /// False for the FIRST rect of a (re)show: x, width and height
    /// interpolate INDEPENDENTLY, so tweening in from the stale rect of the
    /// previous drag would stretch the rect as its edges arrive at
    /// different times. (Hides fade the slot out via SurfaceAnimator and
    /// never move the rect, so they need no gate.) Required like the six
    /// paint properties above, so a host that forgets the forward fails at
    /// instantiation instead of silently never gating.
    required property bool animateMoves

    anchors.fill: parent

    Rectangle {
        x: root.indicatorRect.x
        y: root.indicatorRect.y
        width: root.indicatorRect.width
        height: root.indicatorRect.height

        // Translucent fill plus a solid edge: the fill reads as "this space is
        // claimed" at a glance while staying see-through enough that the
        // windows underneath still orient the drag.
        //
        // The fill alpha REPLACES the colour's own rather than multiplying it.
        // The opacity slider is the one control the user reaches for here, and
        // multiplying would make a picked colour that carries alpha come out
        // darker than the slider says, with no way to tell which of the two
        // was responsible.
        color: Qt.rgba(root.indicatorColor.r, root.indicatorColor.g, root.indicatorColor.b, root.indicatorOpacity)
        // The border carries the picked colour's alpha straight through,
        // matching the snapping zone overlay's border. There is no border
        // opacity slider, so the colour's own channel is the ONE control and
        // no double-apply is possible; the theme fallback resolves opaque, so
        // an unset border still draws solid.
        border.color: root.indicatorBorderColor
        // Zero width is legal and means a fill with no edge, so this is NOT
        // floored at 1 the way a fixed hairline would be.
        border.width: root.indicatorBorderWidth
        radius: root.indicatorBorderRadius

        // Animate the move between targets rather than snapping. The rect
        // changes only when the cursor crosses into a different column or
        // stack slot, so this is a handful of transitions across a drag, and
        // the motion is what makes the new target legible as a CHANGE.
        Behavior on x {
            enabled: root.animateMoves
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
        Behavior on y {
            enabled: root.animateMoves
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
        Behavior on width {
            enabled: root.animateMoves
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
        Behavior on height {
            enabled: root.animateMoves
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
    }
}
