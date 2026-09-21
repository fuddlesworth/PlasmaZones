// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Column {
    id: root
    property string page: "cpu"
    property int minutes: 1
    signal rangeSelected(int value)
    signal networkSettingsRequested
    spacing: 16
    ShellComboBox {
        width: parent.width
        visible: root.page === "gpu" && StatsModel.gpus.length > 1
        model: StatsModel.gpus
        textRole: "name"
        valueRole: "id"
        currentIndex: Math.max(0, StatsModel.gpus.findIndex(g => g.id === StatsModel.gpu.id))
        Accessible.name: qsTr("Graphics device")
        onActivated: AppearanceStore.setValue("statsGpuId", currentValue)
    }
    Loader {
        width: root.width
        sourceComponent: root.page === "cpu" ? cpu : root.page === "gpu" ? gpu : root.page === "memory" ? memory : root.page === "network" ? network : storage
    }
    component Hero: Column {
        id: hero
        property string metric: "cpu"
        property string description: ""
        property string reading: ""
        property string suffix: ""
        width: parent ? parent.width : 0
        spacing: 12
        RowLayout {
            width: parent.width
            spacing: 8
            ShellIcon {
                source: StatsModel.icon(hero.metric)
                isMask: true
                color: Appearance.stops[StatsModel.tone(hero.metric)]
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
            }
            DetailText {
                Layout.fillWidth: true
                text: hero.description
                muted: true
                size: 11
            }
        }
        Row {
            spacing: 9
            DetailText {
                id: heroValue
                text: hero.reading
                size: 43
                font.letterSpacing: -1.5
            }
            DetailText {
                text: hero.suffix
                size: 13
                muted: true
                anchors.baseline: heroValue.baseline
            }
        }
    }
    component Explanation: DetailText {
        width: parent ? parent.width : 0
        size: 10
        muted: true
        lineHeight: 1.4
    }
    Component {
        id: cpu
        Column {
            spacing: 17
            Hero {
                description: StatsModel.cpu.name || qsTr("Processor")
                reading: StatsModel.fixed(StatsModel.cpu.usage, 0)
                suffix: qsTr("% in use")
            }
            StatsHistory {
                minutes: root.minutes
                onRangeSelected: value => root.rangeSelected(value)
            }
            StatsFacts {
                entries: [[qsTr("Clock"), StatsModel.sensor(StatsModel.number(StatsModel.cpu.clock) < 0 ? -1 : StatsModel.cpu.clock / 1000, " GHz", 2)], [qsTr("Temperature"), StatsModel.sensor(StatsModel.cpu.temperature, "°C", 0)], [qsTr("Package power"), StatsModel.sensor(StatsModel.cpu.power, " W", 1)]]
            }
            RowLayout {
                width: parent.width
                DetailText {
                    text: qsTr("Logical processors")
                    size: 10
                    Layout.fillWidth: true
                }
                DetailText {
                    text: StatsModel.number(StatsModel.cpu.cores) > 0 ? qsTr("%1 cores · %2 threads").arg(StatsModel.cpu.cores).arg((StatsModel.cpu.threads || []).length) : qsTr("%1 threads").arg((StatsModel.cpu.threads || []).length)
                    size: 9
                    muted: true
                }
            }
            Grid {
                width: parent.width
                columns: 8
                spacing: 5
                Repeater {
                    model: StatsModel.cpu.threads || []
                    Rectangle {
                        id: thread
                        required property var modelData
                        width: (parent.width - 35) / 8
                        height: 43
                        radius: 4
                        color: Qt.alpha(Appearance.recess, 0.45)
                        border.width: 1
                        border.color: Qt.tint(Appearance.outline, Qt.alpha(Appearance.stops[0], 0.2))
                        Accessible.role: Accessible.StaticText
                        Accessible.name: qsTr("Thread %1, %2").arg(modelData.id + 1).arg(StatsModel.percent(modelData.usage))
                        Rectangle {
                            x: 1
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 1
                            width: parent.width - 2
                            height: Math.max(0, parent.height - 2) * Math.max(0, Math.min(1, thread.modelData.usage / 100))
                            radius: 3
                            color: Qt.alpha(Appearance.stops[0], 0.16)
                        }
                        Column {
                            x: 5
                            y: 6
                            spacing: 4
                            DetailText {
                                text: String(thread.modelData.id + 1).padStart(2, "0")
                                size: 7
                                muted: true
                            }
                            DetailText {
                                text: StatsModel.percent(thread.modelData.usage)
                                size: 10
                            }
                        }
                    }
                }
            }
        }
    }
    Component {
        id: gpu
        Column {
            id: graphics
            readonly property bool available: [StatsModel.gpu.usage, StatsModel.gpu.memoryTotal, StatsModel.gpu.temperature].some(n => StatsModel.number(n) >= 0)
            spacing: 17
            DetailEmptyState {
                visible: !graphics.available
                title: qsTr("GPU readings unavailable")
                description: qsTr("This device is not reporting graphics usage. Other system readings are still available.")
                iconName: "video-card"
            }
            Hero {
                visible: graphics.available
                metric: "gpu"
                description: StatsModel.gpu.name || qsTr("Graphics")
                reading: StatsModel.fixed(StatsModel.gpu.usage, 0)
                suffix: qsTr("% in use")
            }
            StatsHistory {
                visible: graphics.available
                metric: StatsModel.gpuMetric
                tone: 2
                minutes: root.minutes
                onRangeSelected: value => root.rangeSelected(value)
            }
            StatsFacts {
                entries: [[qsTr("Core clock"), StatsModel.sensor(StatsModel.number(StatsModel.gpu.clock) < 0 ? -1 : StatsModel.gpu.clock / 1000, " GHz", 2)], [qsTr("Temperature"), StatsModel.sensor(StatsModel.gpu.temperature, "°C", 0)], [qsTr("Board power"), StatsModel.sensor(StatsModel.gpu.power, " W", 1)]]
            }
            Column {
                visible: graphics.available
                width: parent.width
                spacing: 13
                RowLayout {
                    width: parent.width
                    DetailText {
                        Layout.fillWidth: true
                        text: qsTr("Video memory")
                        size: 10
                    }
                    DetailText {
                        text: qsTr("%1 / %2 GiB").arg(StatsModel.gib(StatsModel.gpu.memoryUsed)).arg(StatsModel.gib(StatsModel.gpu.memoryTotal))
                        muted: true
                        size: 10
                    }
                }
                Rectangle {
                    width: parent.width
                    height: 5
                    radius: 2
                    color: Qt.alpha(Appearance.muted, 0.15)
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, StatsModel.number(StatsModel.gpu.memoryUsed) / Math.max(1, StatsModel.number(StatsModel.gpu.memoryTotal))))
                        height: parent.height
                        radius: 2
                        color: Appearance.stops[2]
                    }
                }
                Explanation {
                    text: qsTr("Dedicated memory used by applications and the desktop.")
                }
                Repeater {
                    model: [[qsTr("Graphics / compute"), StatsModel.gpu.usage], [qsTr("Video encode"), StatsModel.gpu.encode], [qsTr("Video decode"), StatsModel.gpu.decode]]
                    RowLayout {
                        required property var modelData
                        width: parent.width
                        spacing: 12
                        DetailText {
                            text: modelData[0]
                            size: 10
                            Layout.preferredWidth: 115
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 5
                            radius: 2
                            color: Qt.alpha(Appearance.muted, 0.15)
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, StatsModel.number(parent.parent.modelData[1]) / 100))
                                height: parent.height
                                radius: 2
                                color: Appearance.stops[2]
                            }
                        }
                        DetailText {
                            text: StatsModel.percent(modelData[1])
                            size: 10
                            muted: true
                            Layout.preferredWidth: 32
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }
        }
    }
    Component {
        id: memory
        Column {
            spacing: 17
            Hero {
                metric: "memory"
                description: qsTr("%1 GiB · Physical memory").arg(StatsModel.gib(StatsModel.memory.total))
                reading: StatsModel.gib(StatsModel.memory.used)
                suffix: qsTr("GiB in use")
            }
            StatsHistory {
                metric: "memory"
                tone: 1
                minutes: root.minutes
                onRangeSelected: value => root.rangeSelected(value)
            }
            StatsMemoryMeter {
                width: parent.width
                height: 12
            }
            StatsFacts {
                entries: [[qsTr("In use"), StatsModel.bytes(StatsModel.memory.used)], [qsTr("Reclaimable"), StatsModel.bytes(StatsModel.memory.cached)], [qsTr("Free"), StatsModel.bytes(StatsModel.memory.free)]]
            }
            RowLayout {
                width: parent.width
                DetailText {
                    Layout.fillWidth: true
                    text: qsTr("Available to applications")
                    size: 11
                }
                DetailText {
                    text: StatsModel.bytes(StatsModel.memory.available)
                    size: 23
                    color: Appearance.stops[1]
                }
            }
            Explanation {
                text: qsTr("Available memory includes free memory and reclaimable cache. Cached memory can be used by applications when needed.")
            }
            StatsFacts {
                entries: [[qsTr("Swap in use"), qsTr("%1 / %2").arg(StatsModel.bytes(StatsModel.memory.swapUsed)).arg(StatsModel.bytes(StatsModel.memory.swapTotal))]]
            }
        }
    }
    Component {
        id: network
        Column {
            spacing: 17
            DetailEmptyState {
                visible: !StatsModel.network.connected
                title: qsTr("No active connection")
                description: qsTr("Traffic readings will resume when a network connection is available.")
                iconName: "network-wired"
                actionText: qsTr("Network settings")
                onActivated: root.networkSettingsRequested()
            }
            Column {
                visible: !!StatsModel.network.connected
                width: parent.width
                spacing: 17
                Hero {
                    metric: "network"
                    description: (StatsModel.network.wireless ? qsTr("Wi-Fi · %1") : qsTr("Ethernet · %1")).arg(StatsModel.network.interface || "")
                    reading: StatsModel.rateParts(StatsModel.network.down).value
                    suffix: qsTr("%1 down").arg(StatsModel.rateParts(StatsModel.network.down).unit)
                }
                DetailText {
                    text: qsTr("↑ %1 upload").arg(StatsModel.rate(StatsModel.network.up))
                    muted: true
                    size: 11
                }
                StatsHistory {
                    metric: "network"
                    label: qsTr("Download history")
                    minutes: root.minutes
                    onRangeSelected: value => root.rangeSelected(value)
                }
                StatsFacts {
                    entries: [[qsTr("Link speed"), StatsModel.number(StatsModel.network.speed) < 0 ? qsTr("Not reported") : StatsModel.network.speed >= 1000 ? qsTr("%1 Gbps").arg(StatsModel.fixed(StatsModel.network.speed / 1000, 1)) : qsTr("%1 Mbps").arg(StatsModel.fixed(StatsModel.network.speed, 0))], [qsTr("Local address"), StatsModel.network.address || qsTr("Not reported")]]
                }
                DetailText {
                    text: qsTr("Since monitoring started")
                    size: 10
                    muted: true
                }
                RowLayout {
                    width: parent.width
                    spacing: 12
                    Repeater {
                        model: [[qsTr("↓ Downloaded"), StatsModel.network.received], [qsTr("↑ Uploaded"), StatsModel.network.sent]]
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: traffic.implicitHeight + 32
                            radius: Appearance.radius * 0.6
                            color: Qt.alpha(Appearance.card, 0.45)
                            border.width: 1
                            border.color: Appearance.outline
                            Column {
                                id: traffic
                                x: 16
                                y: 16
                                width: parent.width - 32
                                spacing: 9
                                DetailText {
                                    text: parent.parent.modelData[0]
                                    muted: true
                                    size: 10
                                }
                                DetailText {
                                    text: StatsModel.bytes(parent.parent.modelData[1])
                                    size: 24
                                }
                            }
                        }
                    }
                }
                Explanation {
                    text: qsTr("Traffic is shown in bytes per second. Link speed is the connection capacity, in bits per second.")
                }
                ShellButton {
                    text: qsTr("Network settings")
                    iconName: "go-next-symbolic"
                    flat: true
                    foreground: Appearance.muted
                    onClicked: root.networkSettingsRequested()
                }
            }
        }
    }
    Component {
        id: storage
        Column {
            spacing: 17
            RowLayout {
                width: parent.width
                spacing: 8
                DetailText {
                    text: StatsModel.drives.length === 1 ? qsTr("1 local filesystem") : qsTr("%1 local filesystems").arg(StatsModel.drives.length)
                    muted: true
                    size: 11
                    Layout.fillWidth: true
                }
            }
            DetailText {
                width: parent.width
                text: qsTr("Room for what’s next.")
                size: 25
                font.letterSpacing: -0.5
            }
            Explanation {
                text: StatsModel.drives.some(d => d.usage >= 90) ? qsTr("A drive is getting full.") : qsTr("Space and activity, at a glance.")
            }
            StatsDrives {
                detailed: true
            }
            DetailNotice {
                visible: StatsModel.drives.some(d => d.usage >= 90)
                text: qsTr("A drive has less than 10% free space. Move or remove unneeded files to make more room.")
            }
            DetailText {
                text: qsTr("Disk activity · physical drives")
                size: 10
            }
            StatsFacts {
                entries: [[qsTr("Read"), StatsModel.rate(StatsModel.storage.read)], [qsTr("Write"), StatsModel.rate(StatsModel.storage.write)]]
            }
            Explanation {
                text: qsTr("Capacity is shown per local filesystem. Activity combines the read and write rates of physical drives.")
            }
        }
    }
}
