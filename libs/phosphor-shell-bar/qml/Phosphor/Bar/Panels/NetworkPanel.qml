// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.Network

QuickDetailFrame {
    id: root
    title: qsTr("Wi-Fi")
    footerText: root.wifiDevice ? root.wifiDevice.interfaceName : qsTr("Your connection")
    property var serviceHost: host
    property var pointModel: accessPoints
    property var profileModel: profiles
    property var wifiDevice: {
        for (let i = 0; i < root.serviceHost.deviceCount; ++i) {
            const device = root.serviceHost.deviceAt(i);
            if (device && device.deviceType === NetworkDevice.Wifi && device.managed)
                return device;
        }
        return null;
    }
    readonly property bool radioAvailable: serviceHost.available && serviceHost.networkingEnabled && serviceHost.wirelessHardwareEnabled && wifiDevice !== null
    readonly property bool powered: radioAvailable && serviceHost.wirelessEnabled
    readonly property var activeAp: {
        const path = root.wifiDevice ? root.wifiDevice.activeAccessPointPath : "";
        for (let i = 0; i < root.pointModel.count; ++i) {
            const ap = root.pointModel.accessPointAt(i);
            if (ap && ap.dbusPath === path)
                return ap;
        }
        return null;
    }
    readonly property bool connected: powered && wifiDevice.state === NetworkDevice.Activated && activeAp !== null
    readonly property bool portal: serviceHost.connectivity === NetworkHost.Portal
    readonly property bool limited: serviceHost.connectivity === NetworkHost.Limited || serviceHost.connectivity === NetworkHost.NoConnectivity
    readonly property var availablePoints: {
        const networks = new Map();
        for (let i = 0; i < root.pointModel.count; ++i) {
            const ap = root.pointModel.accessPointAt(i);
            if (!ap || (root.connected && root.activeAp && ap.ssid === root.activeAp.ssid && ap.secured === root.activeAp.secured))
                continue;
            const key = ap.ssid + "\u0000" + ap.security;
            const previous = networks.get(key);
            if (!previous || ap.strength > previous.strength)
                networks.set(key, ap);
        }
        return Array.from(networks.values()).sort((a, b) => b.strength - a.strength);
    }
    property var pendingAp: null
    property bool connecting: false
    property bool requestOutstanding: false
    property bool scanning: false
    property double scanStartedAt: -1
    property bool detailsOpen: false
    property bool showPassword: false
    property bool autoConnect: true
    property string errorText: ""
    property string notice: ""
    readonly property bool editing: pendingAp !== null && !connecting
    cancelTask: root.cancel
    NetworkHost {
        id: host
    }
    AccessPointModel {
        id: accessPoints
        device: root.serviceHost === host ? root.wifiDevice : null
    }
    NetworkConnectionModel {
        id: profiles
    }

    function savedProfile(ap): var {
        if (!ap)
            return null;
        for (let i = 0; i < root.profileModel.count; ++i) {
            const profile = root.profileModel.connectionAt(i);
            if (profile && profile.connectionType === "802-11-wireless" && profile.ssid === ap.ssid && (profile.security !== "") === ap.secured)
                return profile;
        }
        return null;
    }
    function scan(): void {
        if (!root.powered || root.connecting)
            return;
        root.scanning = true;
        root.scanStartedAt = root.wifiDevice.lastScan;
        root.errorText = "";
        scanDeadline.restart();
        root.serviceHost.scanWifi();
    }
    function cancel(): bool {
        if (!root.pendingAp && !root.connecting)
            return false;
        if (root.connecting && root.wifiDevice)
            root.serviceHost.disconnectDevice(root.wifiDevice);
        connectionDeadline.stop();
        root.connecting = false;
        root.pendingAp = null;
        root.showPassword = false;
        password.clear();
        root.errorText = "";
        return true;
    }
    function choose(ap): void {
        if (root.connecting || root.requestOutstanding || !ap)
            return;
        root.errorText = "";
        root.notice = "";
        root.pendingAp = ap;
        root.showPassword = false;
        password.clear();
        const profile = root.savedProfile(ap);
        root.autoConnect = profile ? profile.autoConnect : true;
        if (profile || !ap.secured) {
            root.connectNetwork(profile);
        } else if (ap.security === "WEP" || ap.security === "802.1X" || ap.ssid === "") {
            root.pendingAp = null;
            root.errorText = qsTr("This network needs additional configuration in your network manager.");
        } else {
            password.forceActiveFocus();
        }
    }
    function connectNetwork(profile): void {
        if (!root.pendingAp || !root.wifiDevice || root.requestOutstanding)
            return;
        root.connecting = true;
        root.requestOutstanding = true;
        root.errorText = "";
        connectionDeadline.restart();
        if (profile)
            root.serviceHost.activateConnection(profile, root.wifiDevice);
        else
            root.serviceHost.connectToAccessPoint(root.wifiDevice, root.pendingAp, password.text, root.autoConnect, root.savedProfile(root.pendingAp));
        password.clear();
        root.showPassword = false;
    }
    function updateConnection(): void {
        if (!root.connecting || !root.wifiDevice)
            return;
        if (root.connected && root.activeAp === root.pendingAp) {
            connectionDeadline.stop();
            root.connecting = false;
            root.pendingAp = null;
            root.notice = qsTr("Connected to %1.").arg(root.activeAp.ssid);
        } else if (root.wifiDevice.state === NetworkDevice.Failed) {
            connectionDeadline.stop();
            const passwordError = root.wifiDevice.state === NetworkDevice.NeedAuth || root.wifiDevice.stateReason === 7;
            root.connecting = false;
            root.serviceHost.disconnectDevice(root.wifiDevice);
            root.errorText = passwordError ? qsTr("That password didn’t work. Check it and try again.") : qsTr("Couldn’t connect. Check that the network is in range and try again.");
            password.forceActiveFocus();
        }
    }
    function networkMeta(ap): string {
        if (!ap)
            return "";
        const band = ap.frequency >= 5925 ? qsTr("6 GHz") : ap.frequency >= 4900 ? qsTr("5 GHz") : qsTr("2.4 GHz");
        const signal = ap.strength >= 60 ? qsTr("Strong signal") : ap.strength >= 30 ? qsTr("Fair signal") : qsTr("Weak signal");
        return qsTr("%1 · %2 · %3").arg(band).arg(signal).arg(ap.secured ? ap.security : qsTr("Open network"));
    }
    onPoweredChanged: {
        if (root.powered)
            root.scan();
        else {
            root.cancel();
            scanDeadline.stop();
            root.scanning = false;
        }
    }
    onWifiDeviceChanged: {
        root.cancel();
        if (root.powered)
            root.scan();
    }
    onPendingApChanged: if (!root.pendingAp && root.connecting)
        root.cancel()
    Component.onDestruction: root.cancel()
    onActiveApChanged: root.updateConnection()
    Component.onCompleted: if (root.powered)
        root.scan()
    Connections {
        target: root.wifiDevice
        function onStateChanged(): void {
            root.updateConnection();
        }
        function onDetailsChanged(): void {
            if (root.scanning && root.wifiDevice.lastScan !== root.scanStartedAt) {
                root.scanning = false;
                scanDeadline.stop();
            }
        }
    }
    Connections {
        target: root.serviceHost
        function onOperationFinished(operation: string, path: string, errorName: string): void {
            if (operation === "connectToAccessPoint" || operation === "activateConnection") {
                root.requestOutstanding = false;
                if (errorName && root.connecting) {
                    root.connecting = false;
                    connectionDeadline.stop();
                    root.errorText = errorName.includes("NotAuthorized") || errorName.includes("PermissionDenied") ? qsTr("Permission was denied. Check your network permissions and try again.") : qsTr("Couldn’t connect. Check the password and try again.");
                    password.forceActiveFocus();
                }
            } else if (operation === "RequestScan" && errorName) {
                root.scanning = false;
                scanDeadline.stop();
                root.errorText = qsTr("Couldn’t search for networks. Try again in a moment.");
            } else if (errorName) {
                root.errorText = qsTr("The network request was refused. Check your permissions and try again.");
            }
        }
    }
    Timer {
        id: scanDeadline
        interval: 10000
        onTriggered: root.scanning = false
    }
    Timer {
        id: connectionDeadline
        interval: 60000
        onTriggered: {
            root.connecting = false;
            root.serviceHost.disconnectDevice(root.wifiDevice);
            root.errorText = qsTr("The connection timed out. Check the network and try again.");
        }
    }
    headerAction: Component {
        DetailSwitch {
            on: root.serviceHost.wirelessEnabled
            enabled: root.radioAvailable
            Accessible.name: qsTr("Wi-Fi")
            onClicked: root.serviceHost.wirelessEnabled = !root.serviceHost.wirelessEnabled
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
        visible: !root.powered
        iconName: "network-wireless"
        title: !root.radioAvailable ? qsTr("Wi-Fi isn’t available") : qsTr("Wi-Fi is off")
        description: !root.serviceHost.available ? qsTr("NetworkManager isn’t available. Try reconnecting to the service.") : !root.serviceHost.wirelessHardwareEnabled ? qsTr("The wireless adapter is blocked. Check your hardware wireless switch.") : !root.serviceHost.networkingEnabled ? qsTr("Networking is disabled. Enable it in Network Settings.") : !root.wifiDevice ? qsTr("Connect a wireless adapter, then try again.") : qsTr("Turn it on to discover networks nearby.")
        actionText: root.radioAvailable ? qsTr("Turn on Wi-Fi") : qsTr("Try again")
        onActivated: root.radioAvailable ? root.serviceHost.wirelessEnabled = true : root.serviceHost.refresh()
    }
    DetailCard {
        visible: root.connected
        title: root.activeAp ? root.activeAp.ssid : ""
        description: root.portal ? qsTr("Sign in to get internet access.") : root.limited ? qsTr("Connected, with limited internet access.") : qsTr("You’re online. Make yourself at home.")
        iconName: "network-wireless"
        status: root.portal ? qsTr("Action needed") : root.limited ? qsTr("Limited access") : qsTr("Connected")
        compact: root.pendingAp !== null
        DetailText {
            visible: !root.pendingAp
            width: parent.width
            text: root.networkMeta(root.activeAp)
            size: 10
            muted: true
        }
        Rectangle {
            visible: !root.pendingAp
            width: parent.width
            height: 1
            color: Appearance.outline
        }
        RowLayout {
            visible: !root.pendingAp
            width: parent.width
            ShellButton {
                text: root.detailsOpen ? qsTr("Hide details") : qsTr("Details")
                iconName: "arrow-down"
                flat: true
                foreground: Appearance.muted
                onClicked: root.detailsOpen = !root.detailsOpen
            }
            Item {
                Layout.fillWidth: true
            }
            ShellButton {
                text: qsTr("Disconnect")
                flat: true
                foreground: Appearance.muted
                onClicked: root.serviceHost.disconnectDevice(root.wifiDevice)
            }
        }
        GridLayout {
            visible: root.detailsOpen && !root.pendingAp
            width: parent.width
            columns: 2
            rowSpacing: 9
            DetailText {
                text: qsTr("IP address")
                muted: true
                size: 11
            }
            DetailText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: root.wifiDevice && root.wifiDevice.ipAddress ? root.wifiDevice.ipAddress : qsTr("Unavailable")
                size: 11
            }
            DetailText {
                text: qsTr("Link speed")
                muted: true
                size: 11
            }
            DetailText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: root.wifiDevice && root.wifiDevice.bitrate ? qsTr("%1 Mb/s").arg(Math.round(root.wifiDevice.bitrate / 1000)) : qsTr("Unavailable")
                size: 11
            }
            DetailText {
                text: qsTr("Auto-connect")
                muted: true
                size: 11
            }
            DetailText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: root.savedProfile(root.activeAp)?.autoConnect ? qsTr("On") : qsTr("Off")
                size: 11
            }
        }
        ShellButton {
            visible: root.portal && !root.pendingAp
            text: qsTr("Open sign-in")
            highlighted: true
            onClicked: {
                const uri = root.serviceHost.connectivityCheckUri;
                if (/^https?:\/\//i.test(uri))
                    Qt.openUrlExternally(uri);
                else
                    root.errorText = qsTr("Open your browser and visit an HTTP page to sign in to this network.");
            }
        }
    }
    DetailCard {
        visible: root.pendingAp !== null
        tone: 1
        titleSize: 16
        title: root.connecting ? qsTr("Connecting to %1").arg(root.pendingAp?.ssid ?? "") : qsTr("Connect to %1").arg(root.pendingAp?.ssid ?? "")
        description: root.connecting ? qsTr("Checking the connection. This may take a moment.") : qsTr("Enter the password for this network.")
        Basic.BusyIndicator {
            visible: root.connecting
            running: visible && Appearance.motion
            width: 28
            height: 28
        }
        Column {
            visible: root.editing
            width: parent.width
            spacing: 8
            DetailText {
                text: qsTr("Password")
                size: 11
            }
            RowLayout {
                width: parent.width
                spacing: 4
                Basic.TextField {
                    id: password
                    objectName: "wifiPassword"
                    Layout.fillWidth: true
                    implicitHeight: 40
                    echoMode: root.showPassword ? TextInput.Normal : TextInput.Password
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    maximumLength: 64
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round(14 * Appearance.textScale)
                    Accessible.name: qsTr("Network password")
                    background: Rectangle {
                        radius: 8
                        color: Appearance.recess
                        border.width: 1
                        border.color: password.activeFocus ? Appearance.stops[1] : Appearance.outline
                    }
                    onAccepted: if (connectButton.enabled)
                        root.connectNetwork(null)
                }
                ShellButton {
                    iconName: "view-visible"
                    label: root.showPassword ? qsTr("Hide password") : qsTr("Show password")
                    onClicked: root.showPassword = !root.showPassword
                }
            }
            Basic.CheckBox {
                id: autoConnectCheck
                indicator: Rectangle {
                    x: 2
                    y: (parent.height - height) / 2
                    width: 16
                    height: 16
                    radius: 3
                    color: autoConnectCheck.checked ? Qt.alpha(Appearance.stops[1], 0.25) : Appearance.recess
                    border.width: 1
                    border.color: autoConnectCheck.visualFocus ? Appearance.text : Appearance.outline
                    ShellIcon {
                        anchors.centerIn: parent
                        width: 12
                        height: 12
                        visible: autoConnectCheck.checked
                        source: "checkmark"
                        isMask: true
                        color: Appearance.text
                    }
                }
                checked: root.autoConnect
                text: qsTr("Connect automatically")
                onToggled: root.autoConnect = checked
                contentItem: DetailText {
                    text: parent.text
                    size: 11
                    muted: true
                    leftPadding: 28
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
        RowLayout {
            width: parent.width
            Item {
                Layout.fillWidth: true
            }
            ShellButton {
                text: qsTr("Cancel")
                onClicked: root.cancel()
            }
            ShellButton {
                id: connectButton
                objectName: "wifiConnect"
                visible: !root.connecting
                text: qsTr("Connect")
                highlighted: true
                enabled: root.editing && !root.requestOutstanding && ((password.length >= 8 && password.length <= 63) || /^[0-9a-fA-F]{64}$/.test(password.text))
                onClicked: root.connectNetwork(null)
            }
        }
    }
    DetailSectionHeading {
        visible: root.powered
        title: qsTr("Available networks")
        actionText: root.scanning ? qsTr("Searching…") : qsTr("Refresh")
        actionEnabled: !root.scanning && !root.connecting
        onActivated: root.scan()
    }
    DetailList {
        visible: root.powered && root.availablePoints.length > 0
        Repeater {
            model: root.availablePoints
            delegate: DetailDeviceRow {
                required property var modelData
                grouped: true
                title: modelData.ssid || qsTr("Hidden network")
                subtitle: root.networkMeta(modelData) + (root.savedProfile(modelData) ? qsTr(" · Saved") : "")
                iconName: modelData.strength >= 60 ? "network-wireless-signal-excellent" : "network-wireless-signal-ok"
                selected: root.pendingAp === modelData
                trailingIcon: modelData.secured ? "object-locked-symbolic" : "network-connect"
                enabled: !root.connecting && !root.requestOutstanding
                onClicked: root.choose(modelData)
            }
        }
    }
    DetailEmptyState {
        visible: root.powered && root.availablePoints.length === 0 && !root.scanning
        title: qsTr("No networks found")
        description: qsTr("Move closer to your router or search again.")
        iconName: "network-wireless"
        actionText: qsTr("Search again")
        onActivated: root.scan()
    }
    DetailText {
        visible: root.powered
        width: parent.width
        text: qsTr("Secured networks keep your connection private.")
        size: 10
        muted: true
    }
}
