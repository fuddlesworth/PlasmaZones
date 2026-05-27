// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Built-in bar widget: live clock. Plain QML — no C++ side beyond
// the factory that returns one of these.

import Phosphor.Theme
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    implicitWidth: label.implicitWidth + 24
    implicitHeight: Tokens.spacing_xxl
    color: Theme.surface_container
    border.color: Theme.outline_variant
    border.width: 1
    radius: Tokens.radius_s

    Text {
        id: label

        anchors.centerIn: parent
        text: Qt.formatDateTime(currentTime, "ddd MMM d  hh:mm:ss")
        color: Theme.on_surface
        font.pixelSize: Tokens.font_size_body_m
        font.family: Tokens.font_family
        font.weight: Tokens.font_weight_medium

        property date currentTime: new Date()
    }

    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: label.currentTime = new Date()
    }
}
