// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Effects
import Phosphor.Theme

Rectangle {
    id: root
    property string path: ""
    property string fit: "fill"
    property int decodeWidth: 1440
    radius: Math.max(6, Appearance.radius * 0.7)
    color: Appearance.recess
    clip: true
    Image {
        id: picture
        anchors.fill: parent
        source: root.path ? "file://" + encodeURI(root.path).replace(/#/g, "%23").replace(/\?/g, "%3F") : ""
        asynchronous: true
        sourceSize.width: root.fit === "center" ? 0 : root.decodeWidth
        fillMode: root.fit === "fit" ? Image.PreserveAspectFit : root.fit === "stretch" ? Image.Stretch : root.fit === "center" ? Image.Pad : Image.PreserveAspectCrop
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
        }
    }
    Item {
        id: mask
        anchors.fill: parent
        layer.enabled: true
        layer.smooth: true
        visible: false
        Rectangle {
            anchors.fill: parent
            radius: root.radius
            color: "white"
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.color: Appearance.outline
    }
}
