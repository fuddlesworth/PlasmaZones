// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.ControlCenterButton, opens the control center.
//
// A single 8 px dot in the rail's hue at its x, at 70 % (A2 §5). Its
// `activated` signal is relayed through BarRegistry.widgetActivated and
// handled by the shell composer, which hangs the control center pane
// from this bar under this chip.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property real railT: 0.93
    property string label: qsTr("Control center")

    signal activated

    implicitWidth: 20
    implicitHeight: 20

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.onPressAction: root.activated()

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        id: tap

        onTapped: root.activated()
    }

    Rectangle {
        anchors.centerIn: parent
        width: 8
        height: 8
        radius: 4
        color: Spectrum.at(root.railT)
        opacity: hover.hovered ? 1 : 0.7
        scale: tap.pressed ? 0.9 : 1

        Behavior on opacity {
            NumberAnimation {
                duration: hover.hovered ? Motion.duration_enter : Motion.duration_release
                easing: hover.hovered ? Motion.enter : Motion.release
            }
        }
    }
}
