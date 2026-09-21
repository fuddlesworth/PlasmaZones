// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic
import Phosphor.Theme
import Phosphor.Shell

AbstractButton {
    id: root
    property real railT: 0.8
    property bool expanded: false
    signal activated
    implicitWidth: readings.implicitWidth + 16
    implicitHeight: 34
    leftPadding: 8
    rightPadding: 8
    Accessible.name: qsTr("System stats. %1").arg(StatsModel.metrics.map(metric => StatsModel.label(metric) + " " + (metric === "network" ? StatsModel.rate(StatsModel.network.down) : StatsModel.reading(metric))).join(", "))
    onClicked: activated()
    onVisibleChanged: SystemStats.watch(root, visible)
    Component.onCompleted: SystemStats.watch(root, visible)
    Component.onDestruction: SystemStats.watch(root, false)
    background: Rectangle {
        radius: 8
        color: root.expanded ? Qt.tint(Appearance.card, Qt.alpha(Appearance.stops[1], 0.1)) : root.hovered ? Appearance.card : Qt.alpha(Appearance.recess, 0.22)
        border.width: root.visualFocus || root.expanded ? 1 : 0
        border.color: root.visualFocus ? Appearance.text : Appearance.outline
    }
    contentItem: Item {
        StatsReadout {
            id: readings
            anchors.centerIn: parent
        }
    }
    ToolTip.visible: hovered
    ToolTip.delay: 800
    ToolTip.text: qsTr("System stats")
}
