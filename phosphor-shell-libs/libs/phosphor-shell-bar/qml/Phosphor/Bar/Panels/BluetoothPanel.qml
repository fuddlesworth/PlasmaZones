// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.Bluetooth

QuickDetailFrame {
    id: root
    title: qsTr("Bluetooth")
    footerIcon: "network-bluetooth"
    footerText: root.adapter ? root.adapter.alias || root.adapter.name : qsTr("Your devices")
    property var serviceHost: btHost
    property int adapterIndex: 0
    readonly property var adapter: serviceHost.adapterCount > 0 ? serviceHost.adapterAt(Math.min(adapterIndex, serviceHost.adapterCount - 1)) : null
    readonly property var agent: serviceHost.agent
    readonly property bool powered: adapter !== null && adapter.powered
    readonly property var deviceList: {
        const result = [];
        for (let i = 0; i < root.serviceHost.deviceCount; ++i) {
            const device = root.serviceHost.deviceAt(i);
            if (device && root.adapter && device.adapter === root.adapter.dbusPath)
                result.push(device);
        }
        return result.sort((a, b) => root.deviceName(a).localeCompare(root.deviceName(b)));
    }
    readonly property var pairedDevices: deviceList.filter(device => device.paired).sort((a, b) => Number(b.connected) - Number(a.connected))
    readonly property var nearbyDevices: deviceList.filter(device => !device.paired)
    readonly property int connectedCount: deviceList.filter(device => device.connected).length
    property var selectedDevice: null
    property string flow: ""
    property string pendingOperation: ""
    property double requestId: 0
    property string requestKind: ""
    property string pairingCode: ""
    property int enteredDigits: 0
    property string errorText: ""
    property string notice: ""
    property bool ready: false
    property var discoveryAdapter: null
    property bool restoreHidden: false
    readonly property bool busy: pendingOperation !== ""
    cancelTask: root.cancel
    BluetoothHost {
        id: btHost
    }

    function deviceName(device): string {
        return device ? device.alias || device.name || device.address : "";
    }
    function scan(): void {
        if (!root.ready || !root.powered || root.discoveryAdapter || root.busy)
            return;
        root.errorText = "";
        root.discoveryAdapter = root.adapter;
        root.restoreHidden = !root.adapter.discoverable;
        if (root.restoreHidden)
            root.adapter.setDiscoverable(true);
        root.adapter.startDiscovery();
        scanDeadline.restart();
    }
    function stopScan(): void {
        scanDeadline.stop();
        if (root.discoveryAdapter && root.discoveryAdapter.powered) {
            root.discoveryAdapter.stopDiscovery();
            if (root.restoreHidden)
                root.discoveryAdapter.setDiscoverable(false);
        }
        root.restoreHidden = false;
        root.discoveryAdapter = null;
    }
    function cancel(): bool {
        const hadTask = root.flow !== "";
        const device = root.selectedDevice;
        const operation = root.pendingOperation;
        root.pendingOperation = "";
        root.flow = "";
        if (root.requestId && root.agent)
            root.agent.rejectRequest(root.requestId);
        root.requestId = 0;
        root.requestKind = "";
        root.pairingCode = "";
        pin.clear();
        taskDeadline.stop();
        if (device && operation === "Pair")
            device.cancelPairing();
        else if (device && operation === "Connect")
            device.disconnectDevice();
        root.selectedDevice = null;
        root.errorText = "";
        return hadTask;
    }
    function choose(device): void {
        if (root.busy || !device)
            return;
        root.cancel();
        root.notice = "";
        root.selectedDevice = device;
        if (device.paired)
            root.flow = "details";
        else
            root.beginPair();
    }
    function beginPair(): void {
        if (!root.selectedDevice || !root.powered)
            return;
        if (!root.agent || !root.agent.available) {
            root.flow = "error";
            root.errorText = qsTr("The pairing service isn’t ready. Try again in a moment.");
            return;
        }
        root.errorText = "";
        root.flow = "connecting";
        root.pendingOperation = "Pair";
        taskDeadline.restart();
        root.selectedDevice.pair();
    }
    function connectSelected(): void {
        if (!root.selectedDevice || root.busy)
            return;
        root.errorText = "";
        root.pendingOperation = root.selectedDevice.connected ? "Disconnect" : "Connect";
        root.flow = "connecting";
        taskDeadline.restart();
        if (root.pendingOperation === "Disconnect")
            root.selectedDevice.disconnectDevice();
        else
            root.selectedDevice.connectDevice();
    }
    function prompt(path, id, kind, code): void {
        // Only a device the user chose may ask this panel for credentials.
        if (!root.selectedDevice || root.selectedDevice.dbusPath !== path || (root.pendingOperation !== "Pair" && root.pendingOperation !== "Connect")) {
            if (id && root.agent)
                root.agent.rejectRequest(id);
            return;
        }
        if (root.requestId && root.requestId !== id)
            root.agent.rejectRequest(root.requestId);
        root.requestId = id;
        root.requestKind = kind;
        root.pairingCode = code;
        root.flow = kind;
        pin.clear();
        if (kind === "pin" || kind === "passkey")
            pin.forceActiveFocus();
    }
    function respond(accept): void {
        const id = root.requestId;
        if (!id || !root.agent)
            return;
        root.requestId = 0;
        if (!accept) {
            root.agent.rejectRequest(id);
            root.cancel();
            return;
        }
        root.flow = "connecting";
        if (root.requestKind === "pin")
            root.agent.respondPinCode(id, pin.text);
        else if (root.requestKind === "passkey")
            root.agent.respondPasskey(id, Number(pin.text));
        else
            root.agent.respondConfirmation(id, true);
        pin.clear();
    }
    function finish(message): void {
        root.pendingOperation = "";
        root.cancel();
        root.notice = message;
    }
    onAdapterChanged: {
        if (root.ready) {
            root.cancel();
            root.stopScan();
            root.scan();
        }
    }
    onPoweredChanged: {
        if (!root.ready)
            return;
        if (root.powered)
            root.scan();
        else {
            root.cancel();
            root.stopScan();
        }
    }
    onSelectedDeviceChanged: if (!root.selectedDevice && root.busy)
        root.cancel()
    Component.onCompleted: {
        root.ready = true;
        root.scan();
    }
    Component.onDestruction: {
        root.cancel();
        root.stopScan();
    }
    Timer {
        id: scanDeadline
        interval: 60000
        onTriggered: root.stopScan()
    }
    Timer {
        id: taskDeadline
        interval: 120000
        onTriggered: {
            root.cancel();
            root.errorText = qsTr("The device didn’t respond. Keep it nearby and in pairing mode, then try again.");
        }
    }
    Connections {
        target: root.adapter
        function onOperationFinished(operation: string, errorName: string): void {
            if (operation === "RemoveDevice") {
                if (!errorName)
                    root.finish(qsTr("Device forgotten."));
                else {
                    root.pendingOperation = "";
                    root.flow = "details";
                }
            }
            if (errorName && !errorName.endsWith("InProgress")) {
                root.errorText = qsTr("Bluetooth couldn’t complete the request. Check the adapter and try again.");
                if (operation === "StartDiscovery")
                    root.stopScan();
            }
        }
    }
    Connections {
        target: root.selectedDevice
        function onOperationFinished(operation: string, errorName: string): void {
            if (operation !== root.pendingOperation)
                return;
            taskDeadline.stop();
            root.pendingOperation = "";
            if (errorName) {
                if (root.requestId && root.agent)
                    root.agent.rejectRequest(root.requestId);
                root.requestId = 0;
                root.flow = "error";
                root.errorText = errorName.includes("Authentication") ? qsTr("Pairing failed. Check the code and try again.") : qsTr("Couldn’t connect. Keep the device nearby, discoverable, and disconnected from other computers.");
            } else if (operation === "Pair" && root.selectedDevice) {
                root.selectedDevice.setTrusted(true);
                if (root.selectedDevice.connected)
                    root.finish(qsTr("Device paired and connected."));
                else
                    root.connectSelected();
            } else {
                root.finish(operation === "Disconnect" ? qsTr("Device disconnected.") : qsTr("Device connected."));
            }
        }
    }
    Connections {
        target: root.agent
        function onConfirmationRequested(path: string, code: int, id: double): void {
            root.prompt(path, id, "confirm", String(code).padStart(6, "0"));
        }
        function onPinCodeRequested(path: string, id: double): void {
            root.prompt(path, id, "pin", "");
        }
        function onPasskeyRequested(path: string, id: double): void {
            root.prompt(path, id, "passkey", "");
        }
        function onAuthorizationRequested(path: string, id: double): void {
            root.prompt(path, id, "authorize", "");
        }
        function onServiceAuthorizationRequested(path: string, uuid: string, id: double): void {
            root.prompt(path, id, "authorize", uuid);
        }
        function onPinCodeDisplayed(path: string, code: string): void {
            root.prompt(path, 0, "display", code);
        }
        function onPasskeyDisplayed(path: string, code: int, entered: int): void {
            root.prompt(path, 0, "display", String(code).padStart(6, "0"));
            root.enteredDigits = entered;
        }
        function onRequestCancelled(): void {
            if (root.flow === "display") {
                root.flow = "connecting";
                root.pairingCode = "";
            } else if (root.requestId) {
                root.requestId = 0;
                root.cancel();
                root.notice = qsTr("Pairing was cancelled by the device.");
            }
        }
        function onReleased(): void {
            root.cancel();
            root.errorText = qsTr("The pairing service disconnected. Try again.");
        }
    }
    headerAction: Component {
        DetailSwitch {
            on: root.powered
            enabled: root.adapter !== null
            Accessible.name: qsTr("Bluetooth")
            onClicked: root.adapter.setPowered(!root.powered)
        }
    }
    ShellComboBox {
        visible: root.serviceHost.adapterCount > 1
        width: parent.width
        model: {
            const result = [];
            for (let i = 0; i < root.serviceHost.adapterCount; ++i)
                result.push(root.serviceHost.adapterAt(i).alias);
            return result;
        }
        currentIndex: root.adapterIndex
        Accessible.name: qsTr("Bluetooth adapter")
        onActivated: root.adapterIndex = currentIndex
    }
    DetailNotice {
        text: root.errorText
        error: true
    }
    DetailNotice {
        text: root.notice
    }
    DetailEmptyState {
        visible: !root.powered
        iconName: "network-bluetooth"
        title: root.adapter ? qsTr("Bluetooth is off") : qsTr("Bluetooth isn’t available")
        description: root.adapter ? qsTr("Turn it on to connect your headphones, keyboard, and other devices.") : qsTr("Connect a Bluetooth adapter, then try again.")
        actionText: root.adapter ? qsTr("Turn on Bluetooth") : qsTr("Try again")
        onActivated: root.adapter ? root.adapter.setPowered(true) : root.serviceHost.refresh()
    }
    DetailCard {
        visible: root.powered
        tone: 2
        iconName: "network-bluetooth"
        title: qsTr("Your space, connected.")
        description: root.adapter && root.adapter.discoverable ? qsTr("Visible as %1 while discovery is active.").arg(root.deviceName(root.adapter)) : qsTr("Known as %1.").arg(root.deviceName(root.adapter))
        status: root.connectedCount === 1 ? qsTr("1 device connected") : root.connectedCount > 1 ? qsTr("%1 devices connected").arg(root.connectedCount) : qsTr("Not connected")
        compact: root.flow !== ""
        showCompactTitle: false
    }
    DetailCard {
        visible: root.flow !== "" && root.selectedDevice !== null
        tone: 1
        titleSize: 16
        kicker: root.flow === "confirm" || root.flow === "authorize" ? qsTr("PAIRING REQUEST") : root.flow === "pin" || root.flow === "passkey" ? qsTr("DEVICE PIN") : root.flow === "details" ? qsTr("PAIRED DEVICE") : ""
        title: root.flow === "forget" ? qsTr("Forget %1?").arg(root.deviceName(root.selectedDevice)) : root.flow === "details" ? root.deviceName(root.selectedDevice) : root.flow === "error" ? qsTr("Couldn’t connect") : root.flow === "connecting" ? qsTr("Connecting to %1").arg(root.deviceName(root.selectedDevice)) : qsTr("Pair with %1?").arg(root.deviceName(root.selectedDevice))
        description: root.flow === "forget" ? qsTr("You’ll need to pair it again to reconnect.") : root.flow === "confirm" ? qsTr("Check that this code matches the one on your device.") : root.flow === "display" ? qsTr("Type this code on your device, then press Enter.") : root.flow === "pin" || root.flow === "passkey" ? qsTr("Enter the PIN provided by your device.") : root.flow === "authorize" ? qsTr("Allow this device to pair and use its services?") : root.flow === "details" ? (root.selectedDevice?.connected ? qsTr("Connected") : qsTr("Ready to connect")) : qsTr("Keep your device nearby and in pairing mode.")
        Rectangle {
            visible: root.flow === "confirm" || root.flow === "display"
            width: parent.width
            height: 76
            radius: 9
            color: Appearance.recess
            border.width: 1
            border.color: Appearance.outline
            DetailText {
                anchors.centerIn: parent
                text: root.pairingCode
                size: 30
                font.family: Tokens.font_family_mono
                font.letterSpacing: 7
                Accessible.name: qsTr("Pairing code %1").arg(root.pairingCode.split("").join(" "))
            }
        }
        DetailText {
            visible: root.flow === "display"
            text: qsTr("%1 digits entered").arg(root.enteredDigits)
            muted: true
            size: 10
        }
        Basic.TextField {
            id: pin
            objectName: "bluetoothPin"
            visible: root.flow === "pin" || root.flow === "passkey"
            width: parent.width
            implicitHeight: 42
            maximumLength: root.flow === "passkey" ? 6 : 16
            color: Appearance.text
            font.family: Tokens.font_family_mono
            font.pixelSize: 18 * Appearance.textScale
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            Accessible.name: qsTr("Device PIN")
            background: Rectangle {
                radius: 8
                color: Appearance.recess
                border.width: 1
                border.color: pin.activeFocus ? Appearance.stops[1] : Appearance.outline
            }
            onAccepted: if (pairButton.enabled)
                root.respond(true)
        }
        RowLayout {
            width: parent.width
            ShellButton {
                visible: root.flow === "details"
                text: qsTr("Forget…")
                onClicked: root.flow = "forget"
            }
            Item {
                Layout.fillWidth: true
            }
            ShellButton {
                text: qsTr("Cancel")
                onClicked: root.cancel()
            }
            ShellButton {
                id: pairButton
                objectName: "bluetoothConfirm"
                visible: root.flow !== "connecting" && root.flow !== "display"
                text: root.flow === "confirm" ? qsTr("Codes match") : root.flow === "forget" ? qsTr("Forget device") : root.flow === "details" ? (root.selectedDevice?.connected ? qsTr("Disconnect") : qsTr("Connect")) : root.flow === "error" ? qsTr("Try again") : qsTr("Pair device")
                highlighted: true
                enabled: root.pendingOperation !== "RemoveDevice" && (root.flow === "passkey" ? /^[0-9]{1,6}$/.test(pin.text) : root.flow !== "pin" || pin.length > 0)
                onClicked: {
                    if (root.flow === "forget") {
                        root.pendingOperation = "RemoveDevice";
                        root.adapter.removeDevice(root.selectedDevice.dbusPath);
                    } else if (root.flow === "details")
                        root.connectSelected();
                    else if (root.flow === "error") {
                        if (root.selectedDevice.paired)
                            root.connectSelected();
                        else
                            root.beginPair();
                    } else
                        root.respond(true);
                }
            }
        }
        ShellButton {
            visible: root.flow === "confirm"
            width: parent.width
            text: qsTr("The codes don’t match")
            flat: true
            foreground: Appearance.muted
            onClicked: root.respond(false)
        }
    }
    DetailSectionHeading {
        visible: root.powered
        title: qsTr("Your devices")
    }
    DetailList {
        visible: root.powered && root.pairedDevices.length > 0
        Repeater {
            model: root.pairedDevices
            delegate: DetailDeviceRow {
                required property var modelData
                grouped: true
                title: root.deviceName(modelData)
                subtitle: modelData.connected ? qsTr("Connected") : qsTr("Saved · Ready to connect")
                iconName: modelData.icon || "network-bluetooth"
                trailingText: modelData.batteryPercentage >= 0 ? qsTr("%1%").arg(modelData.batteryPercentage) : ""
                selected: modelData === root.selectedDevice
                enabled: !root.busy
                onClicked: root.choose(modelData)
            }
        }
    }
    DetailText {
        visible: root.powered && root.pairedDevices.length === 0
        width: parent.width
        text: qsTr("Paired devices will appear here.")
        muted: true
    }
    DetailSectionHeading {
        visible: root.powered
        title: qsTr("Nearby devices")
        actionText: root.discoveryAdapter ? qsTr("Searching…") : qsTr("Scan")
        actionEnabled: !root.discoveryAdapter && !root.busy
        onActivated: root.scan()
    }
    DetailList {
        visible: root.powered && root.nearbyDevices.length > 0
        Repeater {
            model: root.nearbyDevices
            delegate: DetailDeviceRow {
                required property var modelData
                grouped: true
                title: root.deviceName(modelData)
                subtitle: qsTr("Ready to pair")
                iconName: modelData.icon || "network-bluetooth"
                trailingText: qsTr("Pair")
                trailingIcon: ""
                enabled: !root.busy
                onClicked: root.choose(modelData)
            }
        }
    }
    DetailText {
        visible: root.powered && root.nearbyDevices.length === 0
        width: parent.width
        text: qsTr("No devices found. Put your device in pairing mode, then scan again.")
        muted: true
    }
    DetailText {
        visible: root.powered
        width: parent.width
        text: qsTr("Only pair with devices you recognize.")
        size: 10
        muted: true
    }
}
