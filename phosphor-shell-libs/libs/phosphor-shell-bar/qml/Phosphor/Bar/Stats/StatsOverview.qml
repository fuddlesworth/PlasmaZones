// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme

Column {
    id: root
    signal selected(string metric)
    spacing: 12
    RowLayout {
        width: parent.width
        spacing: 12
        Repeater {
            model: ["cpu", "gpu"]
            StatsCard {
                id: resource
                required property string modelData
                objectName: modelData === "cpu" ? "statsCpuCard" : "statsGpuCard"
                metric: modelData
                title: StatsModel.shortLabel(metric)
                hero: true
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: root.selected(metric)
                Row {
                    spacing: 3
                    DetailText {
                        id: usageValue
                        text: StatsModel.fixed(StatsModel.value(resource.metric), 0)
                        size: Appearance.compact ? 33 : 38
                        font.letterSpacing: -1.5
                    }
                    DetailText {
                        text: StatsModel.value(resource.metric) < 0 ? "" : "%"
                        size: 17
                        muted: true
                        anchors.baseline: usageValue.baseline
                    }
                }
                StatsChart {
                    width: parent.width
                    height: Appearance.compact ? 36 : 43
                    metric: resource.metric === "gpu" ? StatsModel.gpuMetric : "cpu"
                    tint: resource.tint
                }
                RowLayout {
                    width: parent.width
                    spacing: 4
                    DetailText {
                        Layout.fillWidth: true
                        size: 8
                        muted: true
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        text: resource.metric === "cpu" ? qsTr("%1 threads").arg((StatsModel.cpu.threads || []).length) : StatsModel.number(StatsModel.gpu.memoryTotal) > 0 ? qsTr("%1 / %2 GiB").arg(StatsModel.gib(StatsModel.gpu.memoryUsed)).arg(StatsModel.gib(StatsModel.gpu.memoryTotal)) : qsTr("Not reported")
                    }
                    DetailText {
                        text: StatsModel.number(resource.metric === "cpu" ? StatsModel.cpu.temperature : StatsModel.gpu.temperature) < 0 ? "— °C" : StatsModel.fixed(resource.metric === "cpu" ? StatsModel.cpu.temperature : StatsModel.gpu.temperature, 0) + "°C"
                        size: 8
                    }
                }
            }
        }
    }
    StatsCard {
        width: parent.width
        metric: "memory"
        objectName: "statsMemoryCard"
        onClicked: root.selected(metric)
        RowLayout {
            width: parent.width
            Row {
                spacing: 4
                DetailText {
                    id: memoryValue
                    text: StatsModel.gib(StatsModel.memory.used)
                    size: 23
                }
                DetailText {
                    text: qsTr("/ %1 GiB").arg(StatsModel.gib(StatsModel.memory.total))
                    size: 12
                    muted: true
                    anchors.baseline: memoryValue.baseline
                }
            }
            Item {
                Layout.fillWidth: true
            }
            DetailText {
                text: qsTr("%1 used").arg(StatsModel.percent(StatsModel.memory.usage))
                size: 10
                muted: true
            }
        }
        StatsMemoryMeter {
            width: parent.width
        }
        RowLayout {
            width: parent.width
            spacing: 9
            DetailText {
                text: qsTr("In use")
                color: Appearance.stops[1]
                size: 9
            }
            DetailText {
                text: qsTr("Reclaimable")
                muted: true
                size: 9
            }
            Item {
                Layout.fillWidth: true
            }
            DetailText {
                text: qsTr("%1 GiB available").arg(StatsModel.gib(StatsModel.memory.available))
                muted: true
                size: 9
            }
        }
    }
    StatsCard {
        width: parent.width
        metric: "network"
        objectName: "statsNetworkCard"
        status: !StatsModel.network.connected ? qsTr("Disconnected") : StatsModel.network.wireless ? qsTr("Wi-Fi") : qsTr("Ethernet")
        onClicked: root.selected(metric)
        RowLayout {
            width: parent.width
            spacing: 14
            Column {
                spacing: 5
                DetailText {
                    text: qsTr("↓ Download")
                    muted: true
                    size: 9
                }
                DetailText {
                    text: StatsModel.rate(StatsModel.network.down)
                    size: 18
                }
            }
            Column {
                spacing: 5
                DetailText {
                    text: qsTr("↑ Upload")
                    muted: true
                    size: 9
                }
                DetailText {
                    text: StatsModel.rate(StatsModel.network.up)
                    size: 18
                }
            }
            Item {
                Layout.fillWidth: true
            }
            StatsChart {
                Layout.preferredWidth: 70
                Layout.preferredHeight: 30
                metric: "network"
                maximum: Math.max(1000000, StatsModel.number(StatsModel.network.down) * 1.25)
                grid: false
            }
        }
    }
    StatsCard {
        width: parent.width
        metric: "storage"
        objectName: "statsStorageCard"
        status: StatsModel.drives.length > 2 ? qsTr("%1 drives").arg(StatsModel.drives.length) : ""
        onClicked: root.selected(metric)
        StatsDrives {
            width: parent.width
        }
    }
}
