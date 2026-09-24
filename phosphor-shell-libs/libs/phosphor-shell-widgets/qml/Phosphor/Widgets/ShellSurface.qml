// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Effects
import Phosphor.Theme

Rectangle {
    id: root
    property real railT: 0.5
    property bool accented: false
    property bool barSurface: false
    property bool shadowed: true
    radius: Appearance.radius
    color: Qt.alpha(Appearance.surface, Appearance.surfaceOpacity)
    border.width: 1
    border.color: Appearance.outline
    RectangularShadow {
        anchors.fill: parent
        z: -1
        visible: root.shadowed
        radius: root.radius
        blur: root.barSurface ? 25 : 55
        offset: Qt.vector2d(0, root.barSurface ? 8 : 22)
        color: Qt.alpha(Qt.darker(Appearance.recess, 1.5), root.barSurface ? 0.2 : Appearance.light ? 0.25 : 0.5)
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, root.radius - 1)
        gradient: Gradient {
            GradientStop {
                position: 0
                color: Qt.alpha(Appearance.accent, 0.07)
            }
            GradientStop {
                position: 0.55
                color: "transparent"
            }
        }
    }
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.radius
        anchors.rightMargin: root.radius
        height: 1
        color: "#10ffffff"
    }
    Rectangle {
        visible: root.accented
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.barSurface ? 14 : 22
        anchors.rightMargin: root.barSurface ? 14 : 22
        height: 2
        radius: 1
        opacity: root.barSurface ? 0.8 : 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: Appearance.stops[0]
            }
            GradientStop {
                position: 0.34
                color: Appearance.stops[1]
            }
            GradientStop {
                position: 0.68
                color: Appearance.stops[2]
            }
            GradientStop {
                position: 1
                color: Appearance.stops[3]
            }
        }
    }
}
