// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Row {
    id: root
    property var metrics: StatsModel.metrics
    property string style: StatsModel.style
    spacing: 6
    Repeater {
        model: root.metrics
        Item {
            id: cell
            required property string modelData
            width: Math.round(46 * Appearance.textScale)
            height: root.style === "numbers" ? 16 : 25
            readonly property color tint: Appearance.stops[StatsModel.tone(modelData)]
            readonly property real reading: StatsModel.value(modelData)
            Text {
                anchors.left: parent.left
                y: 1
                text: StatsModel.shortLabel(cell.modelData)
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: Math.round(7 * Appearance.textScale)
            }
            Text {
                anchors.right: parent.right
                text: StatsModel.reading(cell.modelData)
                color: root.style === "numbers" ? cell.tint : Appearance.text
                font.family: Tokens.font_family_mono
                font.pixelSize: Math.round(10 * Appearance.textScale)
            }
            StatsChart {
                visible: root.style === "traces"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 8
                metric: cell.modelData === "gpu" ? StatsModel.gpuMetric : cell.modelData
                maximum: cell.modelData === "memory" ? Math.max(1, StatsModel.number(StatsModel.memory.total)) : cell.modelData === "network" ? Math.max(1000000, cell.reading * 1.25) : 100
                tint: cell.tint
                grid: false
            }
            Row {
                visible: root.style === "meters"
                anchors.bottom: parent.bottom
                width: parent.width
                height: 7
                spacing: 2
                Repeater {
                    model: 10
                    Rectangle {
                        required property int index
                        width: (cell.width - 18) / 10
                        height: 7
                        radius: 1
                        readonly property real level: cell.modelData === "network" ? (StatsModel.number(StatsModel.network.speed) > 0 ? cell.reading * 8 / (StatsModel.network.speed * 1000000) * 100 : -1) : cell.reading
                        color: level >= 0 && index < level / 10 ? cell.tint : Qt.alpha(Appearance.muted, 0.18)
                    }
                }
            }
        }
    }
}
