// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Polkit.PolkitDim, the rest of the screen at 20%.
//
// While a request is open the screen dims 20% (A3 §9c, consistency table
// "dim: polkit uses 20%") and the requesting window does not: `hole` is
// its rect, left clear by drawing the dim as four rectangles around it.
// With no hole the dim is one rectangle. It is click-through by nature
// (the host mounts it on a surface with an empty input region) and
// animates on the dim envelope: 600 ms out, 350 ms back.
//
//   PolkitDim { anchors.fill: parent; active: request !== null; hole: anchor.rect }

import QtQuick
import Phosphor.Theme

Item {
    id: dim

    property bool active: false
    // The requester's rect in this item's coordinates, or null.
    property var hole: null

    readonly property real level: 0.2
    readonly property bool holed: dim.hole !== null && dim.hole !== undefined && dim.hole.width > 0 && dim.hole.height > 0

    opacity: dim.active ? 1 : 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation {
            duration: dim.active ? Motion.duration_long_4 : Motion.duration_medium_3
            easing: Motion.release
        }
    }

    // Above the hole.
    Rectangle {
        x: 0
        y: 0
        width: dim.width
        height: dim.holed ? Math.max(0, dim.hole.y) : dim.height
        color: Theme.background
        opacity: dim.level
    }

    // Below the hole.
    Rectangle {
        visible: dim.holed
        x: 0
        y: dim.holed ? dim.hole.y + dim.hole.height : 0
        width: dim.width
        height: dim.holed ? Math.max(0, dim.height - y) : 0
        color: Theme.background
        opacity: dim.level
    }

    // Left of the hole.
    Rectangle {
        visible: dim.holed
        x: 0
        y: dim.holed ? dim.hole.y : 0
        width: dim.holed ? Math.max(0, dim.hole.x) : 0
        height: dim.holed ? dim.hole.height : 0
        color: Theme.background
        opacity: dim.level
    }

    // Right of the hole.
    Rectangle {
        visible: dim.holed
        x: dim.holed ? dim.hole.x + dim.hole.width : 0
        y: dim.holed ? dim.hole.y : 0
        width: dim.holed ? Math.max(0, dim.width - x) : 0
        height: dim.holed ? dim.hole.height : 0
        color: Theme.background
        opacity: dim.level
    }
}
