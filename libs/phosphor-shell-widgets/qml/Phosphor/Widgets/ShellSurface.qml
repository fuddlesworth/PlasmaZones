// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Rectangle {
    id: root
    property real railT: 0.5
    property bool accented: true
    radius: Appearance.radius
    color: Qt.alpha(Appearance.surface, Appearance.glass ? 0.96 : 1)
    border.width: 1
    border.color: Appearance.outline

    // A quiet wash gives the surface depth without competing with content.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, root.radius - 1)
        gradient: Gradient {
            GradientStop {
                position: 0
                color: Qt.alpha(Appearance.at(root.railT), Appearance.glow ? 0.15 : 0.05)
            }
            GradientStop {
                position: 0.65
                color: "transparent"
            }
        }
    }
    Rectangle {
        visible: root.accented
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.radius
        anchors.rightMargin: root.radius
        height: 2
        opacity: Appearance.glow ? 0.8 : 0.45
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: Appearance.at(Math.max(0, root.railT - 0.2))
            }
            GradientStop {
                position: 1
                color: Appearance.at(Math.min(1, root.railT + 0.2))
            }
        }
    }
}
