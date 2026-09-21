// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Device fixtures: every write remains inside this preview process.
import QtQuick
import Phosphor.Shell
import Phosphor.Bar
import Phosphor.Theme
import Phosphor.Ipc

Item {
    id: root
    property string view: "wifi"
    property var networkPanel: null
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
            NetworkPanel {
                id: panel
                anchors.right: parent.right
                anchors.rightMargin: 24
                y: 96
                width: implicitWidth
                height: Math.min(implicitHeight, parent.height - 112)
                serviceHost: wifi
                pointModel: points
                profileModel: profiles
                onBackRequested: root.view = "back"
                onCloseRequested: root.view = "closed"
                Component.onCompleted: root.networkPanel = panel
            }
        }
    }
    IpcTarget {
        target: "preview"
        function state(): string {
            return JSON.stringify({
                view: root.view,
                connecting: root.networkPanel.connecting,
                error: root.networkPanel.errorText,
                connected: root.networkPanel.connected
            });
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
        function preset(name: string): bool {
            return AppearanceStore.applyPreset(name);
        }
    }
}
