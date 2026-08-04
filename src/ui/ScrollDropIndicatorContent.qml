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

    anchors.fill: parent

    Rectangle {
        x: root.indicatorRect.x
        y: root.indicatorRect.y
        width: root.indicatorRect.width
        height: root.indicatorRect.height

        // Translucent fill plus a solid edge: the fill reads as "this space is
        // claimed" at a glance while staying see-through enough that the
        // windows underneath still orient the drag.
        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.25)
        border.color: Kirigami.Theme.highlightColor
        border.width: Math.max(1, Math.round(Kirigami.Units.smallSpacing / 2))
        radius: Kirigami.Units.smallSpacing

        // Animate the move between targets rather than snapping. The rect
        // changes only when the cursor crosses into a different column or
        // stack slot, so this is a handful of transitions across a drag, and
        // the motion is what makes the new target legible as a CHANGE.
        Behavior on x {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
        Behavior on y {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
        Behavior on width {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
        Behavior on height {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
    }
}
