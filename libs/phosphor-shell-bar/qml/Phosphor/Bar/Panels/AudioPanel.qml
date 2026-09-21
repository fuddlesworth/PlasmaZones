// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.PipeWire

QuickDetailFrame {
    id: root
    title: qsTr("Audio")
    footerIcon: "audio-volume-high"
    footerText: qsTr("Sound, in your hands.")
    property var serviceHost: PipeWireHost
    property var sinkModel: sinks
    property var sourceModel: sources
    property var streamModel: streams
    property var audioProbe: probe
    property string tab: "output"
    property int modelRevision: 0
    property string errorText: ""
    property string notice: ""
    property string requestedDevice: ""
    property string requestedKind: ""
    readonly property var sink: serviceHost.defaultSink
    readonly property var source: serviceHost.defaultSource
    readonly property var device: tab === "input" ? source : sink
    readonly property var outputs: root.nodes(root.sinkModel)
    readonly property var inputs: root.nodes(root.sourceModel)
    readonly property var applications: root.nodes(root.streamModel).filter(node => node.mediaClass === "Stream/Output/Audio" && node.applicationName !== "Phosphor audio test")
    readonly property var deviceChoices: tab === "input" ? inputs : outputs
    readonly property var outputLabels: [qsTr("Default output")].concat(root.outputs.map(node => root.deviceName(node)))
    PwSinkModel {
        id: sinks
        connection: root.serviceHost === PipeWireHost ? PipeWireHost.connection : null
    }
    PwSourceModel {
        id: sources
        connection: root.serviceHost === PipeWireHost ? PipeWireHost.connection : null
    }
    PwStreamModel {
        id: streams
        connection: root.serviceHost === PipeWireHost ? PipeWireHost.connection : null
    }
    PwAudioProbe {
        id: probe
    }
    function nodes(model): var {
        void root.modelRevision;
        const result = [];
        for (let i = 0; i < model.count; ++i) {
            const node = model.nodeAt(i);
            if (node)
                result.push(node);
        }
        return result;
    }
    function deviceName(node): string {
        return node ? node.description || node.nick || node.name : "";
    }
    function deviceMeta(node): string {
        if (!node)
            return "";
        const connection = node.name.startsWith("bluez") ? qsTr("Bluetooth") : node.name.includes("usb") ? qsTr("USB audio") : qsTr("Audio device");
        return connection + (node.channelCount ? " · " + (node.channelCount === 1 ? qsTr("Mono") : node.channelCount === 2 ? qsTr("Stereo") : qsTr("%1 channels").arg(node.channelCount)) : "");
    }
    function setTab(index): void {
        root.tab = ["output", "input", "apps"][index];
        tabs.itemAt(index).forceActiveFocus();
    }
    function selectDevice(node): void {
        if (!node)
            return;
        root.audioProbe.stop();
        root.errorText = "";
        root.requestedDevice = node.name;
        root.requestedKind = root.tab;
        switchDeadline.restart();
        if (root.tab === "input")
            root.serviceHost.connection.setDefaultSource(node.name);
        else
            root.serviceHost.connection.setDefaultSink(node.name);
    }
    function checkSelection(): void {
        if (!root.requestedDevice)
            return;
        const selected = root.requestedKind === "input" ? root.source : root.sink;
        if (selected && selected.name === root.requestedDevice) {
            root.notice = qsTr("Using %1.").arg(root.deviceName(selected));
            root.requestedDevice = "";
            switchDeadline.stop();
        }
    }
    function routeIndex(node): int {
        if (!node.targetName || node.targetName === "-1")
            return 0;
        return root.outputs.findIndex(output => output.name === node.targetName || output.serial === node.targetName) + 1 || -1;
    }
    onTabChanged: root.audioProbe.stop()
    onSourceChanged: {
        root.audioProbe.stop();
        root.checkSelection();
    }
    onSinkChanged: {
        if (root.audioProbe.playing)
            root.audioProbe.stop();
        root.checkSelection();
    }
    Component.onDestruction: root.audioProbe.stop()
    Timer {
        id: switchDeadline
        interval: 5000
        onTriggered: {
            root.requestedDevice = "";
            root.errorText = qsTr("The audio device didn’t change. Check the audio service and try again.");
        }
    }
    Connections {
        target: root.serviceHost
        function onConnectedChanged(): void {
            if (!root.serviceHost.connected) {
                root.audioProbe.stop();
                root.requestedDevice = "";
                switchDeadline.stop();
            }
        }
    }
    Connections {
        target: root.serviceHost.connection
        function onOperationFailed(message: string): void {
            root.errorText = message;
            root.requestedDevice = "";
            switchDeadline.stop();
        }
        function onNodeRemoved(node: var): void {
            if (node.mediaClass === "Audio/Sink" || node.mediaClass === "Audio/Source")
                root.notice = qsTr("%1 disconnected.").arg(root.deviceName(node));
        }
    }
    Connections {
        target: root.audioProbe
        function onError(message: string): void {
            root.errorText = message;
        }
    }
    Connections {
        target: root.source
        function onPropsChanged(): void {
            if (root.source && root.source.muted && root.audioProbe.listening)
                root.audioProbe.stop();
        }
    }
    Connections {
        target: root.sinkModel
        ignoreUnknownSignals: true
        function onModelReset(): void {
            root.modelRevision++;
        }
        function onRowsInserted(): void {
            root.modelRevision++;
        }
        function onRowsRemoved(): void {
            root.modelRevision++;
        }
    }
    Connections {
        target: root.sourceModel
        ignoreUnknownSignals: true
        function onModelReset(): void {
            root.modelRevision++;
        }
        function onRowsInserted(): void {
            root.modelRevision++;
        }
        function onRowsRemoved(): void {
            root.modelRevision++;
        }
    }
    Connections {
        target: root.streamModel
        ignoreUnknownSignals: true
        function onModelReset(): void {
            root.modelRevision++;
        }
        function onRowsInserted(): void {
            root.modelRevision++;
        }
        function onRowsRemoved(): void {
            root.modelRevision++;
        }
    }
    DetailNotice {
        text: root.errorText
        error: true
    }
    DetailNotice {
        text: root.notice
    }
    DetailEmptyState {
        visible: !root.serviceHost.connected
        iconName: "audio-volume-muted"
        title: qsTr("Sound service unavailable")
        description: qsTr("Your volume settings are kept. Try reconnecting to the audio service.")
        actionText: qsTr("Try again")
        onActivated: root.serviceHost.reconnect()
    }
    Rectangle {
        visible: root.serviceHost.connected
        width: parent.width
        height: tabLayout.implicitHeight + 8
        radius: 10
        color: Qt.alpha(Appearance.recess, 0.55)
        border.width: 1
        border.color: Appearance.outline
        Accessible.role: Accessible.PageTabList
        Accessible.name: qsTr("Audio controls")
        RowLayout {
            id: tabLayout
            x: 4
            y: 4
            width: parent.width - 8
            spacing: 3
            Repeater {
                id: tabs
                model: [qsTr("Output"), qsTr("Input"), qsTr("Apps")]
                delegate: ShellButton {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    implicitHeight: 36
                    labelSize: 12
                    iconName: ["audio-headphones", "audio-input-microphone", "view-grid"][index]
                    flat: root.tab !== ["output", "input", "apps"][index]
                    highlighted: !flat
                    activeFocusOnTab: !flat
                    Accessible.role: Accessible.PageTab
                    Accessible.selected: !flat
                    onClicked: root.setTab(index)
                    Keys.onRightPressed: event => {
                        root.setTab((index + 1) % 3);
                        event.accepted = true;
                    }
                    Keys.onLeftPressed: event => {
                        root.setTab((index + 2) % 3);
                        event.accepted = true;
                    }
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                            root.setTab(event.key === Qt.Key_Home ? 0 : 2);
                            event.accepted = true;
                        }
                    }
                }
            }
        }
    }
    DetailEmptyState {
        visible: root.serviceHost.connected && root.tab !== "apps" && root.deviceChoices.length === 0
        iconName: root.tab === "input" ? "audio-input-microphone" : "audio-headphones"
        title: root.tab === "input" ? qsTr("No input devices") : qsTr("No output devices")
        description: qsTr("Connect a speaker, headset, or microphone to get started.")
    }
    DetailCard {
        visible: root.serviceHost.connected && root.tab !== "apps" && root.deviceChoices.length > 0
        title: root.device ? root.deviceName(root.device) : root.tab === "input" ? qsTr("Choose an input") : qsTr("Choose an output")
        description: root.tab === "input" ? (root.device?.muted ? qsTr("Your microphone is muted.") : qsTr("Make yourself heard.")) : qsTr("Sound, right where you want it.")
        iconName: root.tab === "input" ? "audio-input-microphone" : root.device?.iconName || "audio-headphones"
        status: !root.device ? "" : root.device.muted ? qsTr("Muted") : root.tab === "input" ? qsTr("Default input") : qsTr("Default output")
        DetailVolume {
            node: root.device
            label: root.tab === "input" ? qsTr("Input volume") : qsTr("Output volume")
            microphone: root.tab === "input"
        }
        Row {
            visible: root.tab === "input"
            width: parent.width
            height: 17
            spacing: 3
            Accessible.name: root.audioProbe.listening ? qsTr("Microphone signal %1 percent").arg(Math.round(root.audioProbe.level * 100)) : qsTr("Microphone test idle")
            Repeater {
                model: 28
                delegate: Rectangle {
                    required property int index
                    width: (parent.width - 27 * 3) / 28
                    height: 17
                    radius: 2
                    // A decibel scale makes quiet speech readable; idle stays dark.
                    readonly property real meter: root.audioProbe.level > 0 ? Math.max(0, (20 * Math.log(root.audioProbe.level) / Math.LN10 + 60) / 60) : 0
                    color: root.audioProbe.listening && index < meter * 28 ? Appearance.stops[0] : Qt.alpha(Appearance.muted, 0.18)
                }
            }
        }
        RowLayout {
            visible: root.tab === "input"
            width: parent.width
            DetailText {
                Layout.fillWidth: true
                text: root.audioProbe.listening ? qsTr("Listening…") : qsTr("Check your microphone")
                size: 10
                muted: true
            }
            ShellButton {
                text: root.audioProbe.listening ? qsTr("Stop test") : qsTr("Test microphone")
                flat: true
                enabled: root.source !== null && !root.source.muted
                onClicked: {
                    root.errorText = "";
                    if (root.audioProbe.listening)
                        root.audioProbe.stop();
                    else
                        root.audioProbe.startInputTest(root.source.name);
                }
            }
        }
    }
    DetailSectionHeading {
        visible: root.serviceHost.connected && root.tab !== "apps" && root.deviceChoices.length > 0
        title: root.tab === "input" ? qsTr("Record sound from") : qsTr("Play sound through")
    }
    DetailList {
        visible: root.serviceHost.connected && root.tab !== "apps" && root.deviceChoices.length > 0
        Accessible.role: Accessible.Grouping
        Accessible.name: root.tab === "input" ? qsTr("Input device") : qsTr("Output device")
        Repeater {
            id: deviceRows
            model: root.deviceChoices
            delegate: DetailDeviceRow {
                required property int index
                required property var modelData
                grouped: true
                radio: true
                title: root.deviceName(modelData)
                subtitle: root.deviceMeta(modelData)
                iconName: root.tab === "input" ? "audio-input-microphone" : modelData.iconName || "audio-headphones"
                selected: root.device === modelData
                activeFocusOnTab: selected || !root.device && index === 0
                onClicked: root.selectDevice(modelData)
                function moveSelection(step): void {
                    const next = (index + step + root.deviceChoices.length) % root.deviceChoices.length;
                    root.selectDevice(root.deviceChoices[next]);
                    deviceRows.itemAt(next).forceActiveFocus();
                }
                Keys.onDownPressed: event => {
                    moveSelection(1);
                    event.accepted = true;
                }
                Keys.onUpPressed: event => {
                    moveSelection(-1);
                    event.accepted = true;
                }
            }
        }
    }
    ShellButton {
        visible: root.serviceHost.connected && root.tab === "output" && root.sink !== null
        text: root.audioProbe.playing ? qsTr("Playing…") : qsTr("Play test sound")
        iconName: "audio-volume-high"
        flat: true
        foreground: Appearance.muted
        enabled: root.sink !== null && !root.sink.muted && !root.audioProbe.playing
        onClicked: {
            root.errorText = "";
            root.audioProbe.playTestSound(root.sink.name);
        }
    }
    Column {
        visible: root.serviceHost.connected && root.tab === "apps"
        width: parent.width
        spacing: 8
        DetailText {
            text: qsTr("YOUR MIX")
            size: 9
            muted: true
            font.letterSpacing: 1.6
        }
        DetailText {
            text: qsTr("A place for every sound.")
            size: 22
            font.weight: Font.Medium
        }
        DetailText {
            width: parent.width
            text: qsTr("Balance active apps and choose where each one plays.")
            muted: true
        }
    }
    Column {
        visible: root.serviceHost.connected && root.tab === "apps"
        width: parent.width
        spacing: 12
        Repeater {
            model: root.applications
            delegate: DetailCard {
                id: appCard
                horizontal: true
                padding: 16
                required property var modelData
                titleSize: 14
                title: appCard.modelData.applicationName || root.deviceName(appCard.modelData)
                description: appCard.modelData.mediaName || root.deviceName(appCard.modelData)
                iconName: appCard.modelData.iconName || "audio-x-generic"
                iconFallback: "audio-x-generic"
                status: appCard.modelData.muted ? qsTr("Muted") : appCard.modelData.running ? qsTr("Playing") : qsTr("Paused")
                tone: 2
                DetailVolume {
                    node: appCard.modelData
                    label: qsTr("%1 volume").arg(appCard.title)
                }
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Appearance.outline
                }
                RowLayout {
                    width: parent.width
                    DetailText {
                        Layout.fillWidth: true
                        text: qsTr("Play through")
                        size: 10
                        muted: true
                    }
                    ShellComboBox {
                        Layout.preferredWidth: Math.min(210, parent.width * 0.68)
                        model: root.outputLabels
                        currentIndex: root.routeIndex(appCard.modelData)
                        displayText: currentIndex < 0 ? qsTr("Disconnected output") : currentText
                        Accessible.name: qsTr("%1 output").arg(appCard.modelData.applicationName || root.deviceName(appCard.modelData))
                        enabled: appCard.modelData.canMove
                        onActivated: root.serviceHost.connection.setStreamTarget(appCard.modelData.id, currentIndex === 0 ? "" : root.outputs[currentIndex - 1].name)
                    }
                }
            }
        }
    }
    DetailEmptyState {
        visible: root.serviceHost.connected && root.tab === "apps" && root.applications.length === 0
        iconName: "applications-multimedia"
        title: qsTr("Nothing is playing yet")
        description: qsTr("Start some music or a video. Its volume controls will appear here.")
    }
    DetailText {
        visible: root.serviceHost.connected
        width: parent.width
        text: root.tab === "apps" ? qsTr("Apps appear here when they play sound.") : root.tab === "input" ? qsTr("Testing stops when you leave this view.") : qsTr("Apps using the default output follow this device.")
        size: 10
        muted: true
    }
}
