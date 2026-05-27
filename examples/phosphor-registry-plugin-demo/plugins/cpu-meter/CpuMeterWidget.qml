// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// CPU meter widget bundled inside the plugin's qrc-baked QML
// module. Shows a "live" reading via a sine-driven bar — no
// real /proc/stat reads; this demo proves the plugin ABI, not
// actual system monitoring.

import QtQuick

Rectangle {
    id: root

    implicitWidth: 120
    implicitHeight: 32
    color: "#1f2228"
    border.color: "#3b4048"
    border.width: 1
    radius: 4

    property real reading: 0

    Timer {
        interval: 200
        repeat: true
        running: true
        onTriggered: root.reading = 0.5 + 0.5 * Math.sin(Date.now() / 1000)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: (parent.width - 8) * root.reading
        height: parent.height - 8
        color: "#4ea1ff"
        radius: 2
    }

    Text {
        anchors.centerIn: parent
        text: "CPU " + Math.round(root.reading * 100) + "%"
        color: "#e8eaee"
        font.pixelSize: 12
        font.family: "monospace"
    }
}
