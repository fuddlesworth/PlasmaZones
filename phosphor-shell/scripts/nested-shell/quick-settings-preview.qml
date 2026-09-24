// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Wi-Fi / Bluetooth writes remain in this process. Audio requires a private
// PipeWire fixture, supplied with PIPEWIRE_RUNTIME_DIR by the caller.
import QtQuick
import Phosphor.Shell
import Phosphor.Bar
import Phosphor.Theme
import Phosphor.Ipc

Item {
    id: root
    property string view: "wifi"
    property bool audioEnabled: false
    property var networkPanel: null
    property var bluetoothPanel: null
    property var audioPanel: null
    QtObject {
        id: wifi
        property bool available: true
        property bool networkingEnabled: true
        property bool wirelessHardwareEnabled: true
        property bool wirelessEnabled: true
        property int deviceCount: 1
        property int connectivity: 4
        property string connectivityCheckUri: "https://example.test"
        signal operationFinished(string operation, string path, string errorName)
        function deviceAt(index) {
            return index === 0 ? adapter : null;
        }
        function scanWifi() {
            adapter.lastScan++;
        }
        function refresh() {
            available = true;
        }
        function disconnectDevice(device) {
            device.state = 30;
            device.activeAccessPointPath = "";
        }
        function connectToAccessPoint(device, ap, password, autoConnect) {
            operationFinished("connectToAccessPoint", device.dbusPath, "");
        }
        function activateConnection(profile, device) {
            operationFinished("activateConnection", device.dbusPath, "");
        }
    }
    QtObject {
        id: adapter
        property string dbusPath: "/fixture/wifi"
        property int deviceType: 2
        property bool managed: true
        property int state: 100
        property int stateReason: 0
        property string interfaceName: "wlan0"
        property string activeAccessPointPath: "/fixture/home"
        property string ipAddress: "192.168.1.24"
        property int bitrate: 866000
        property double lastScan: 1
        signal detailsChanged
        onLastScanChanged: detailsChanged()
    }
    QtObject {
        id: home
        property string dbusPath: "/fixture/home"
        property string ssid: "Home network"
        property int strength: 92
        property int frequency: 5180
        property bool secured: true
        property string security: "WPA3"
    }
    QtObject {
        id: studio
        property string dbusPath: "/fixture/studio"
        property string ssid: "Studio guest"
        property int strength: 78
        property int frequency: 2437
        property bool secured: true
        property string security: "WPA2"
    }
    QtObject {
        id: cafe
        property string dbusPath: "/fixture/cafe"
        property string ssid: "Corner café"
        property int strength: 51
        property int frequency: 2437
        property bool secured: false
        property string security: ""
    }
    QtObject {
        id: points
        property int count: 3
        function accessPointAt(i) {
            return [home, studio, cafe][i];
        }
    }
    QtObject {
        id: profiles
        property int count: 0
        function connectionAt(i) {
            return null;
        }
    }
    PerScreenPanels {
        model: PhosphorShell.screens
        delegate: PanelWindow {
            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.Exclusive
            Loader {
                anchors.right: parent.right
                anchors.rightMargin: 24
                y: 96
                width: 410
                height: Math.min(item ? item.implicitHeight : 0, parent.height - 112)
                sourceComponent: root.view === "audio" ? audioComponent : root.view === "bluetooth" ? bluetoothComponent : networkComponent
            }
        }
    }
    Component {
        id: networkComponent
        NetworkPanel {
            id: panel
            serviceHost: wifi
            pointModel: points
            profileModel: profiles
            onBackRequested: root.view = "back"
            onCloseRequested: root.view = "closed"
            Component.onCompleted: root.networkPanel = panel
        }
    }
    Component {
        id: audioComponent
        AudioPanel {
            id: panel
            onBackRequested: root.view = "back"
            onCloseRequested: root.view = "closed"
            Component.onCompleted: root.audioPanel = panel
        }
    }
    Component {
        id: bluetoothComponent
        BluetoothPanel {
            id: panel
            serviceHost: bluetooth
            onBackRequested: root.view = "back"
            onCloseRequested: root.view = "closed"
            Component.onCompleted: root.bluetoothPanel = panel
        }
    }
    QtObject {
        id: bluetooth
        property int adapterCount: 1
        property int deviceCount: 3
        property var agent: pairingAgent
        function adapterAt(i) {
            return i === 0 ? bluetoothAdapter : null;
        }
        function deviceAt(i) {
            return [headphones, mouse, keyboard][i];
        }
        function refresh() {
            adapterCount = 1;
        }
    }
    QtObject {
        id: bluetoothAdapter
        property string dbusPath: "/fixture/bluetooth"
        property string name: "Phosphor Desktop"
        property bool powered: true
        property bool discoverable: false
        property bool discovering: false
        signal operationFinished(string operation, string errorName)
        function setPowered(value) {
            powered = value;
        }
        function setDiscoverable(value) {
            discoverable = value;
        }
        function startDiscovery() {
            discovering = true;
        }
        function stopDiscovery() {
            discovering = false;
        }
        function removeDevice(path) {
            headphones.paired = false;
            headphones.connected = false;
            operationFinished("RemoveDevice", "");
        }
    }
    component FixtureBluetoothDevice: QtObject {
        property string dbusPath: ""
        property string adapter: "/fixture/bluetooth"
        property string name: ""
        property string icon: "network-bluetooth"
        property bool paired: false
        property bool connected: false
        property int batteryPercentage: -1
        property bool trusted: false
        signal operationFinished(string operation, string errorName)
        function pair() {
            pairingAgent.confirmationRequested(dbusPath, 428163, 1);
        }
        function cancelPairing() {
            operationFinished("Pair", "org.bluez.Error.AuthenticationCanceled");
        }
        function setTrusted(value) {
            trusted = value;
        }
        function connectDevice() {
            connected = true;
            operationFinished("Connect", "");
        }
        function disconnectDevice() {
            connected = false;
            operationFinished("Disconnect", "");
        }
    }
    FixtureBluetoothDevice {
        id: headphones
        dbusPath: "/fixture/headphones"
        name: "Headphones"
        icon: "audio-headphones"
        paired: true
        connected: true
        batteryPercentage: 78
    }
    FixtureBluetoothDevice {
        id: mouse
        dbusPath: "/fixture/mouse"
        name: "MX Master"
        icon: "input-mouse"
        paired: true
        connected: true
        batteryPercentage: 62
    }
    FixtureBluetoothDevice {
        id: keyboard
        dbusPath: "/fixture/keyboard"
        name: "Orbit Keyboard"
        icon: "input-keyboard"
    }
    QtObject {
        id: pairingAgent
        property bool available: true
        property string response: ""
        signal confirmationRequested(string path, int code, double id)
        signal pinCodeRequested(string path, double id)
        signal passkeyRequested(string path, double id)
        signal authorizationRequested(string path, double id)
        signal serviceAuthorizationRequested(string path, string uuid, double id)
        signal pinCodeDisplayed(string path, string code)
        signal passkeyDisplayed(string path, int code, int entered)
        signal requestCancelled
        signal released
        function respondConfirmation(id, accepted) {
            response = accepted ? "accepted" : "rejected";
            keyboard.paired = accepted;
            keyboard.operationFinished("Pair", accepted ? "" : "org.bluez.Error.Rejected");
        }
        function respondPinCode(id, pin) {
            respondConfirmation(id, true);
        }
        function respondPasskey(id, key) {
            respondConfirmation(id, true);
        }
        function rejectRequest(id) {
            response = "rejected";
        }
    }
    IpcTarget {
        target: "preview"
        function state(): string {
            return JSON.stringify({
                view: root.view,
                connecting: root.networkPanel?.connecting ?? false,
                error: root.networkPanel?.errorText ?? root.bluetoothPanel?.errorText ?? "",
                connected: root.networkPanel?.connected ?? false,
                bluetoothFlow: root.bluetoothPanel?.flow ?? "",
                pairingResponse: pairingAgent.response,
                keyboardConnected: keyboard.connected,
                audioTab: root.audioPanel?.tab ?? "",
                audioError: root.audioPanel?.errorText ?? "",
                inputTesting: root.audioPanel?.audioProbe.listening ?? false,
                inputLevel: root.audioPanel?.audioProbe.level ?? 0
            });
        }
        function show(name: string): bool {
            if (name === "audio" && !root.audioEnabled)
                return false;
            root.view = name;
            return true;
        }
        function bluetoothState(name: string): bool {
            if (!root.bluetoothPanel)
                return false;
            root.bluetoothPanel.cancel();
            bluetooth.adapterCount = 1;
            bluetoothAdapter.powered = true;
            keyboard.paired = false;
            keyboard.connected = false;
            pairingAgent.response = "";
            if (name === "pair" || name === "pin" || name === "passkey" || name === "error")
                root.bluetoothPanel.choose(keyboard);
            if (name === "pin")
                pairingAgent.pinCodeRequested(keyboard.dbusPath, 2);
            if (name === "passkey")
                pairingAgent.passkeyRequested(keyboard.dbusPath, 2);
            if (name === "error")
                keyboard.operationFinished("Pair", "org.bluez.Error.AuthenticationFailed");
            if (name === "off")
                bluetoothAdapter.powered = false;
            if (name === "unavailable")
                bluetooth.adapterCount = 0;
            return true;
        }
        function wifiState(name: string): bool {
            root.networkPanel.cancel();
            wifi.available = true;
            wifi.wirelessEnabled = true;
            adapter.state = 100;
            adapter.activeAccessPointPath = home.dbusPath;
            if (name === "password" || name === "connecting" || name === "error")
                root.networkPanel.choose(studio);
            if (name === "connecting")
                root.networkPanel.connectNetwork(null);
            if (name === "error")
                root.networkPanel.errorText = "That password didn’t work. Check it and try again.";
            if (name === "off")
                wifi.wirelessEnabled = false;
            if (name === "unavailable")
                wifi.available = false;
            if (name === "details")
                root.networkPanel.detailsOpen = true;
            wifi.connectivity = name === "portal" ? 2 : 4;
            return true;
        }
        function audioTab(name: string): bool {
            if (!root.audioPanel)
                return false;
            root.audioPanel.tab = name;
            return true;
        }
        function preset(name: string): bool {
            return AppearanceStore.applyPreset(name);
        }
    }
}
