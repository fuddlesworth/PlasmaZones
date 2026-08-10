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
    /// Configured fill and border colours. EMPTY means "follow the theme",
    /// which is the shipped default for both.
    required property string indicatorColor
    required property string indicatorBorderColor
    /// Fill opacity. Applies to the fill only; the border's transparency
    /// comes from its own colour's alpha channel.
    required property real indicatorOpacity
    required property int indicatorBorderWidth
    required property int indicatorBorderRadius
    /// Whether a rect change should be ANIMATED. True for a cursor-driven
    /// target change, which is what the transitions exist to make legible.
    /// False only on the clear/hide paths (cross-screen hide, preview-end
    /// teardown): a rectangle that must stop being painted has no target to
    /// make legible, and x, width and height interpolate INDEPENDENTLY, so
    /// animating a hide would stretch the rect as its edges arrive at
    /// different times.
    property bool animateMoves: true

    /// Resolved paint colours. The empty test is the only fallback: the D-Bus
    /// boundary rejects anything that is neither empty nor a colour QColor can
    /// parse, so nothing the SETTINGS APP writes can arrive unparseable.
    ///
    /// A hand-edited config.json is not covered by that guard — it reaches
    /// here through P_STORE_GET unvalidated. Qt turns an unparseable string
    /// into an invalid QColor, which paints as black rather than falling back
    /// to the scheme colour, so the failure is a visibly wrong indicator
    /// rather than a crash or a silently absent one. Left as is deliberately:
    /// re-validating in QML would duplicate the boundary check in a second
    /// place that cannot report the problem to anyone.
    readonly property color fillColor: root.indicatorColor === "" ? Kirigami.Theme.highlightColor : root.indicatorColor
    readonly property color edgeColor: root.indicatorBorderColor === "" ? Kirigami.Theme.highlightColor : root.indicatorBorderColor

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
        color: Qt.rgba(root.fillColor.r, root.fillColor.g, root.fillColor.b, root.indicatorOpacity)
        // The border carries the picked colour's alpha straight through,
        // matching the snapping zone overlay's border. There is no border
        // opacity slider, so the colour's own channel is the ONE control and
        // no double-apply is possible; the theme fallback is opaque, so an
        // unset border still draws solid.
        border.color: root.edgeColor
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
