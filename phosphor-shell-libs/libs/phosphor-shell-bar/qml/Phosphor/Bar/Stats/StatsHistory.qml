// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Rectangle {
    id: root
    property string metric: "cpu"
    property string label: qsTr("Usage history")
    property int minutes: 1
    property int tone: 0
    readonly property real maximum: metric === "memory" ? Math.max(1, StatsModel.number(StatsModel.memory.total)) : metric === "network" ? Math.max(1000, ...plot.samples.map(p => Math.max(0, p.value) * 1.15)) : 100
    signal rangeSelected(int value)
    width: parent ? parent.width : 0
    implicitHeight: content.implicitHeight + 24
    radius: Appearance.radius * 0.65
    color: Qt.alpha(Appearance.recess, 0.3)
    border.width: 1
    border.color: Appearance.outline
    Column {
        id: content
        x: 14
        y: 12
        width: parent.width - 28
        spacing: 13
        RowLayout {
            width: parent.width
            DetailText {
                Layout.fillWidth: true
                text: root.label
                size: 10
            }
            Row {
                spacing: 2
                Repeater {
                    model: [1, 5, 15]
                    ShellButton {
                        required property int modelData
                        text: qsTr("%1m").arg(modelData)
                        label: modelData === 1 ? qsTr("1 minute history") : qsTr("%1 minute history").arg(modelData)
                        implicitWidth: 31
                        implicitHeight: 25
                        labelSize: 9
                        highlighted: root.minutes === modelData
                        flat: !highlighted
                        onClicked: root.rangeSelected(modelData)
                    }
                }
            }
        }
        Item {
            width: parent.width
            height: 123
            StatsChart {
                id: plot
                width: parent.width - 37
                height: 120
                metric: root.metric
                maximum: root.maximum
                minutes: root.minutes
                tint: Appearance.stops[root.tone]
            }
            DetailText {
                anchors.right: parent.right
                y: -3
                text: root.metric === "memory" ? StatsModel.bytes(root.maximum) : root.metric === "network" ? StatsModel.rate(root.maximum) : "100%"
                size: 8
                muted: true
            }
            DetailText {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                text: "0"
                size: 8
                muted: true
            }
        }
        RowLayout {
            width: parent.width - 37
            DetailText {
                text: root.minutes === 1 ? qsTr("1 minute ago") : qsTr("%1 minutes ago").arg(root.minutes)
                muted: true
                size: 8
                Layout.fillWidth: true
            }
            DetailText {
                text: qsTr("Now")
                muted: true
                size: 8
            }
        }
    }
}
