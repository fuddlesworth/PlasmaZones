// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Built-in bar widget: a coloured square. Demonstrates that the
// registry seam doesn't care what the widget actually does — pure
// chrome that cycles through accent colours to prove the slot is
// alive.

import Phosphor.Theme
import QtQuick

Rectangle {
    id: root

    implicitWidth: Tokens.spacing_xxl
    implicitHeight: Tokens.spacing_xxl
    color: cycleColors[colorIndex]
    border.color: Theme.outline_variant
    border.width: 1
    radius: Tokens.radius_s

    property int colorIndex: 0
    readonly property var cycleColors: [Theme.primary, Theme.tertiary, Theme.secondary, Theme.error]

    Timer {
        interval: 1500
        repeat: true
        running: true
        onTriggered: root.colorIndex = (root.colorIndex + 1) % root.cycleColors.length
    }
}
