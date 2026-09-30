// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Device and service controls use local fixtures. Appearance writes stay in
// the preview's private XDG config home. Audio requires a private PipeWire
// fixture, supplied with PIPEWIRE_RUNTIME_DIR by the caller.
import QtQuick
import QtQuick.Layouts
import Phosphor.Shell
import Phosphor.Bar
import Phosphor.ControlCenter
import Phosphor.Theme
import Phosphor.Ipc

Item {
    id: root
    property string view: "wifi"
    property bool audioEnabled: false
    property var networkPanel: null
    property var bluetoothPanel: null
    property var audioPanel: null
    property var activePanel: null
    property int previewHeight: 0
    property int previewWidth: 410
    property string appearancePage: ""
    property bool statusVisible: false
    property string statusPage: "settings"
    property string statusDetailRequest: ""
    readonly property var views: ["wifi", "bluetooth", "audio", "nightlight", "power", "airplane", "darkmode", "wallpaper", "battery", "status-icons", "controlcenter"]
    function showStatusPage(page: string): bool {
        if (!["settings", "overflow"].concat(statusFixture.ids).includes(page))
            return false;
        statusVisible = true;
        statusPage = page;
        statusDetailRequest = "";
        if (view === "status-icons" && activePanel)
            activePanel.page = page;
        view = "status-icons";
        return true;
    }
    function openStatusDetail(panelId: string): void {
        statusDetailRequest = panelId || "controlcenter";
        const target = panelId === "network" ? "wifi" : panelId || "controlcenter";
        if (views.includes(target) && (target !== "audio" || audioEnabled))
            view = target;
    }
    function wallpaperPath(name): string {
        return decodeURIComponent(Qt.resolvedUrl(name).toString().replace(/^file:\/\//, ""));
    }
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
            Rectangle {
                id: statusPreviewCard
                anchors.right: parent.right
                anchors.rightMargin: Tokens.spacing_xl
                y: Appearance.barHeight + Tokens.spacing_xl * 2
                width: Math.min(480, parent.width - Tokens.spacing_xl * 2)
                height: previewControls.implicitHeight + Tokens.spacing_l * 2
                visible: root.statusVisible
                radius: Tokens.radius_m
                color: Appearance.card
                border.width: 1
                border.color: Appearance.outline
                RowLayout {
                    id: previewControls
                    anchors.fill: parent
                    anchors.margins: Tokens.spacing_l
                    spacing: Tokens.spacing_l
                    DetailText {
                        Layout.fillWidth: true
                        text: i18n("Status icon preview")
                        font.pixelSize: Tokens.font_size_body_s
                        muted: true
                    }
                    ControlCenterButton {
                        id: statusPill
                        controller: statusFixture
                        quickSettingsOpen: root.view === "controlcenter"
                        expandedStatusPage: root.view === "status-icons" && root.activePanel ? root.activePanel.page : ""
                        onActivated: {
                            if (requestedStatusPage)
                                root.showStatusPage(requestedStatusPage);
                            else
                                root.openStatusDetail("");
                        }
                    }
                }
            }
            Loader {
                anchors.right: parent.right
                anchors.rightMargin: 24
                y: root.statusVisible ? statusPreviewCard.y + statusPreviewCard.height + Tokens.spacing_m : 96
                width: root.view === "status-icons" && status === Loader.Ready ? item.implicitWidth : root.previewWidth
                height: Math.min(item ? item.implicitHeight : 0, root.previewHeight > 0 ? root.previewHeight : parent.height - y - Tokens.spacing_l)
                onItemChanged: root.activePanel = item
                active: root.views.indexOf(root.view) >= 0
                sourceComponent: root.view === "status-icons" ? statusComponent : root.view === "controlcenter" ? controlCenterComponent : root.view === "audio" ? audioComponent : root.view === "bluetooth" ? bluetoothComponent : root.view === "nightlight" ? nightLightComponent : root.view === "power" ? powerComponent : root.view === "airplane" ? airplaneComponent : root.view === "darkmode" ? darkModeComponent : root.view === "wallpaper" ? wallpaperComponent : root.view === "battery" ? batteryComponent : networkComponent
            }
        }
    }
    StatusIconsPreviewController {
        id: statusFixture
        network: wifi
        networkAdapter: adapter
        bluetoothHost: bluetooth
        bluetoothRadio: bluetoothAdapter
        battery: batteryHost
        batteryDevice: laptopBattery
        quickController: quickSettings
        airplaneController: airplane
    }
    Component {
        id: statusComponent
        StatusIconsPanel {
            controller: statusFixture
            page: root.statusPage
            maximumHeight: root.previewHeight > 0 ? root.previewHeight : Math.max(220, Screen.height - 112)
            onDetailRequested: panelId => root.openStatusDetail(panelId)
            onCloseRequested: root.view = "closed"
        }
    }
    Component {
        id: controlCenterComponent
        ControlCenter {
            spectrum: null
            focusEnabled: statusFixture.focusEnabled
            nightLightEnabled: quickSettings.nightLightRunning
            nightLightAvailable: quickSettings.nightLightAvailable
            batterySummary: statusFixture.icons.find(item => item.id === "battery").summary
            powerSummary: statusFixture.icons.find(item => item.id === "power").summary
            nightLightSummary: statusFixture.icons.find(item => item.id === "nightlight").summary
            airplaneSummary: statusFixture.icons.find(item => item.id === "airplane").summary
            darkModeSummary: colorMode.effectiveDark ? i18n("Dark") : i18n("Light")
            darkModeEnabled: colorMode.effectiveDark
            airplaneEnabled: airplane.enabled
            wallpaperSummary: i18n("Spectrum")
            onFocusToggled: statusFixture.toggle("focus")
            onNightLightToggled: statusFixture.toggle("nightlight")
            onPanelRequested: panelId => root.openStatusDetail(panelId)
            onCloseRequested: root.view = "closed"
        }
    }
    Component {
        id: nightLightComponent
        NightLightPanel {
            controller: quickSettings
            onBackRequested: root.view = "back"
            onCloseRequested: root.view = "closed"
        }
    }
    Component {
        id: powerComponent
        PowerProfilesPanel {
            controller: quickSettings
            onPanelRequested: panelId => root.view = panelId
            onBackRequested: root.view = "back"
            onCloseRequested: root.view = "closed"
        }
    }
    Component {
        id: airplaneComponent
        AirplanePanel {
            controller: airplane
            onBackRequested: root.view = "back"
            onCloseRequested: root.view = "closed"
        }
    }
    Component {
        id: darkModeComponent
        DarkModePanel {
            controller: colorMode
            onAppearanceRequested: page => root.appearancePage = page
            onBackRequested: root.view = "back"
            onCloseRequested: root.view = "closed"
        }
    }
    Component {
        id: wallpaperComponent
        WallpaperPanel {
            screenName: root.wallpaperScreen
            onAppearanceRequested: page => root.appearancePage = page
            onBackRequested: root.view = "back"
            onCloseRequested: root.view = "closed"
        }
    }
    Component {
        id: batteryComponent
        BatteryPanel {
            controller: quickSettings
            serviceHost: batteryHost
            deviceModel: batteryDevices
            onPanelRequested: panel => root.view = panel
            onBackRequested: root.view = "back"
            onCloseRequested: root.view = "closed"
        }
    }
    QtObject {
        id: quickSettings
        property bool nightLightAvailable: true
        property bool nightLightEnabled: true
        property bool nightLightRunning: true
        property bool nightLightInhibited: false
        property bool nightLightPaused: false
        property bool nightLightScheduleAvailable: true
        property int nightLightTemperature: 4200
        property string nightLightSchedule: "automatic"
        property string nightLightMorning: "06:30"
        property string nightLightEvening: "20:00"
        property bool nightLightPending: false
        property string nightLightError: ""
        property bool powerAvailable: true
        property string powerProfile: "balanced"
        property var powerProfiles: ["power-saver", "balanced", "performance"]
        property string performanceDegraded: ""
        property bool powerPending: false
        property string powerError: ""
        function setNightLightEnabled(value): void {
            if (!nightLightAvailable || nightLightPending)
                return;
            nightLightEnabled = value;
            nightLightRunning = value && !nightLightInhibited;
        }
        function setNightLightTemperature(value): void {
            if (nightLightAvailable && !nightLightPending && value >= 1000 && value <= 6500)
                nightLightTemperature = value;
        }
        function setNightLightSchedule(value): void {
            if (!nightLightPending && (value === "manual" || nightLightScheduleAvailable))
                nightLightSchedule = value;
        }
        function setNightLightTimes(morning, evening): void {
            const valid = /^(?:[01][0-9]|2[0-3]):[0-5][0-9]$/;
            if (!valid.test(morning) || !valid.test(evening) || morning === evening) {
                nightLightError = "Choose different valid start and end times";
                return;
            }
            nightLightMorning = morning;
            nightLightEvening = evening;
            nightLightSchedule = "custom";
            nightLightError = "";
        }
        function toggleNightLightPause(): void {
            if (!nightLightAvailable || !nightLightEnabled || nightLightPending)
                return;
            nightLightPaused = !nightLightPaused;
            nightLightInhibited = nightLightPaused;
            nightLightRunning = !nightLightPaused;
        }
        function refreshNightLight(): void {
            nightLightAvailable = true;
            nightLightPending = false;
            nightLightError = "";
        }
        function setPowerProfile(value): void {
            if (!powerAvailable || powerPending || powerProfiles.indexOf(value) < 0)
                return;
            powerProfile = value;
            powerError = "";
        }
        function refreshPower(): void {
            powerAvailable = true;
            powerPending = false;
            powerError = "";
        }
    }
    QtObject {
        id: airplane
        property bool available: true
        property bool enabled: false
        property bool pending: false
        property string error: ""
        readonly property bool wifiAvailable: wifi.available
        readonly property bool wifiEnabled: wifi.wirelessEnabled
        readonly property bool wifiHardwareEnabled: wifi.wirelessHardwareEnabled
        property bool wwanAvailable: true
        property bool wwanEnabled: true
        property bool wwanHardwareEnabled: true
        readonly property bool bluetoothAvailable: bluetooth.adapterCount > 0
        readonly property bool bluetoothEnabled: bluetoothAdapter.powered
        property bool restoreAvailable: false
        property var saved: ({
                wifi: true,
                bluetooth: true,
                wwan: true
            })
        function setEnabled(value): void {
            if (!available || pending)
                return;
            if (!value) {
                restoreRadios();
                return;
            }
            saved = {
                wifi: wifiEnabled,
                bluetooth: bluetoothEnabled,
                wwan: wwanEnabled
            };
            wifi.wirelessEnabled = false;
            bluetoothAdapter.powered = false;
            wwanEnabled = false;
            enabled = true;
            error = "";
        }
        function setWifiEnabled(value): void {
            if (pending || !wifiAvailable || (value && !wifiHardwareEnabled))
                return;
            wifi.wirelessEnabled = value;
            if (value)
                enabled = false;
        }
        function setBluetoothEnabled(value): void {
            if (!pending && bluetoothAvailable)
                bluetoothAdapter.powered = value;
        }
        function setWwanEnabled(value): void {
            if (pending || !wwanAvailable || (value && !wwanHardwareEnabled))
                return;
            wwanEnabled = value;
            if (value)
                enabled = false;
        }
        function restoreRadios(): void {
            if (pending)
                return;
            wifi.wirelessEnabled = saved.wifi;
            bluetoothAdapter.powered = saved.bluetooth;
            wwanEnabled = saved.wwan;
            enabled = false;
            restoreAvailable = false;
            error = "";
        }
        function refresh(): void {
            available = true;
            pending = false;
            error = "";
        }
    }
    QtObject {
        id: colorMode
        property string mode: "dark"
        property bool systemAvailable: true
        property bool systemDark: true
        readonly property bool effectiveDark: mode === "dark" || mode === "system" && systemDark
        property bool previewActive: false
        property string error: ""
        function setMode(value): void {
            if (["light", "dark", "system"].indexOf(value) < 0 || value === "system" && !systemAvailable)
                return;
            mode = value;
            AppearanceStore.setValue("material", effectiveDark ? "glass" : "light");
        }
        function refresh(): void {
            systemAvailable = true;
            error = "";
        }
    }
    component FixtureBattery: QtObject {
        property string model: ""
        property int type: 2
        property bool isPresent: true
        readonly property bool isLaptopBattery: type === 2
        property real percentage: 82
        property int state: 2
        property real timeToEmpty: 21600
        property real timeToFull: 0
        property real healthPercentage: 92
        property real energyRate: 8.2
        property real energy: 49.2
        property real energyCapacity: 60
        property string iconName: "battery-good"
    }
    FixtureBattery {
        id: laptopBattery
        model: "Laptop battery"
    }
    FixtureBattery {
        id: headphoneBattery
        model: "Headphones"
        type: 19
        percentage: 78
        timeToEmpty: 0
        healthPercentage: 0
        iconName: "audio-headphones"
    }
    FixtureBattery {
        id: mouseBattery
        model: "MX Master"
        type: 5
        percentage: 62
        timeToEmpty: 0
        healthPercentage: 0
        iconName: "input-mouse"
    }
    QtObject {
        id: batteryHost
        property bool available: true
        property bool pending: false
        property bool onBattery: true
        property var displayDevice: laptopBattery
        function refresh(): void {
            available = true;
            pending = false;
        }
    }
    ListModel {
        id: batteryDevices
    }
    property string wallpaperScreen: ""
    Component.onCompleted: {
        batteryDevices.append({
            device: laptopBattery
        });
        batteryDevices.append({
            device: headphoneBattery
        });
        batteryDevices.append({
            device: mouseBattery
        });
        AppearanceStore.setWallpaper(root.wallpaperPath("fixture-spectrum.png"), "", "fill");
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
                panelReady: root.activePanel !== null,
                panelTitle: root.activePanel?.title ?? "",
                connecting: root.networkPanel?.connecting ?? false,
                error: root.networkPanel?.errorText ?? root.bluetoothPanel?.errorText ?? "",
                connected: root.networkPanel?.connected ?? false,
                bluetoothFlow: root.bluetoothPanel?.flow ?? "",
                pairingResponse: pairingAgent.response,
                keyboardConnected: keyboard.connected,
                audioTab: root.audioPanel?.tab ?? "",
                audioError: root.audioPanel?.errorText ?? "",
                inputTesting: root.audioPanel?.audioProbe.listening ?? false,
                inputLevel: root.audioPanel?.audioProbe.level ?? 0,
                appearancePage: root.appearancePage,
                status: {
                    page: root.view === "status-icons" ? root.activePanel?.page ?? root.statusPage : "",
                    scenario: statusFixture.scenario,
                    detailRequest: root.statusDetailRequest,
                    icons: statusFixture.icons,
                    shown: statusFixture.shownIcons.map(item => item.id),
                    overflow: statusFixture.overflowIcons.map(item => item.id),
                    limit: statusFixture.maxVisible,
                    batteryPercentage: statusFixture.showBatteryPercentage,
                    volume: statusFixture.volume,
                    muted: statusFixture.muted,
                    microphoneMuted: statusFixture.microphoneMuted,
                    outputs: statusFixture.outputs
                },
                nightLight: {
                    available: quickSettings.nightLightAvailable,
                    enabled: quickSettings.nightLightEnabled,
                    paused: quickSettings.nightLightPaused,
                    temperature: quickSettings.nightLightTemperature,
                    schedule: quickSettings.nightLightSchedule,
                    morning: quickSettings.nightLightMorning,
                    evening: quickSettings.nightLightEvening,
                    pending: quickSettings.nightLightPending,
                    error: quickSettings.nightLightError
                },
                power: {
                    available: quickSettings.powerAvailable,
                    profile: quickSettings.powerProfile,
                    profiles: quickSettings.powerProfiles,
                    pending: quickSettings.powerPending,
                    error: quickSettings.powerError
                },
                airplane: {
                    available: airplane.available,
                    enabled: airplane.enabled,
                    wifi: airplane.wifiEnabled,
                    bluetooth: airplane.bluetoothEnabled,
                    wwan: airplane.wwanEnabled,
                    pending: airplane.pending,
                    restoreAvailable: airplane.restoreAvailable,
                    error: airplane.error
                },
                colorMode: {
                    mode: colorMode.mode,
                    effectiveDark: colorMode.effectiveDark,
                    systemAvailable: colorMode.systemAvailable
                },
                battery: {
                    available: batteryHost.available,
                    present: laptopBattery.isPresent,
                    percentage: laptopBattery.percentage,
                    state: laptopBattery.state
                },
                wallpaper: {
                    screen: root.wallpaperScreen,
                    editing: AppearanceStore.editing,
                    assignments: AppearanceStore.values.wallpapers
                }
            });
        }
        function show(name: string): bool {
            if (root.views.indexOf(name) < 0 || name === "audio" && !root.audioEnabled)
                return false;
            root.appearancePage = "";
            if (name === "status-icons")
                return root.showStatusPage("settings");
            root.view = name;
            return true;
        }
        function statusState(name: string): bool {
            if (!statusFixture.setScenario(name))
                return false;
            return root.showStatusPage(root.statusPage);
        }
        function statusPage(name: string): bool {
            return root.showStatusPage(name);
        }
        function statusVisibility(id: string, policy: string): bool {
            return statusFixture.setVisibility(id, policy);
        }
        function statusMove(id: string, beforeId: string): bool {
            return statusFixture.moveBefore(id, beforeId);
        }
        function statusStep(id: string, delta: int): bool {
            return statusFixture.moveIcon(id, delta);
        }
        function statusLimit(value: int): bool {
            return statusFixture.setMaxVisible(value);
        }
        function statusPercentage(value: bool): bool {
            return statusFixture.setShowBatteryPercentage(value);
        }
        function statusReset(): bool {
            return statusFixture.resetIcons();
        }
        function statusToggle(id: string): bool {
            return statusFixture.toggle(id);
        }
        function statusVolume(value: int): bool {
            return statusFixture.setVolume(value);
        }
        function statusOutput(id: string): bool {
            return statusFixture.selectOutput(id);
        }
        function statusPower(id: string): bool {
            return statusFixture.setPowerProfile(id);
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
            if (!root.networkPanel)
                return false;
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
        function nightLightState(name: string): bool {
            if (["ready", "custom", "manual", "paused", "inhibited", "off", "scheduled", "unavailable", "schedule-unavailable", "pending", "error"].indexOf(name) < 0)
                return false;
            quickSettings.nightLightAvailable = name !== "unavailable";
            quickSettings.nightLightEnabled = name !== "off";
            quickSettings.nightLightPaused = name === "paused";
            quickSettings.nightLightInhibited = name === "paused" || name === "inhibited";
            quickSettings.nightLightRunning = ["off", "paused", "inhibited", "scheduled", "unavailable"].indexOf(name) < 0;
            quickSettings.nightLightScheduleAvailable = name !== "schedule-unavailable";
            quickSettings.nightLightTemperature = 4200;
            quickSettings.nightLightSchedule = name === "custom" ? "custom" : name === "manual" ? "manual" : "automatic";
            quickSettings.nightLightMorning = "06:30";
            quickSettings.nightLightEvening = "20:00";
            quickSettings.nightLightPending = name === "pending";
            quickSettings.nightLightError = name === "error" ? "Settings were saved but Night light could not apply them" : "";
            root.view = "nightlight";
            return true;
        }
        function powerState(name: string): bool {
            if (["ready", "power-saver", "performance", "degraded", "lap", "limited", "unavailable", "pending", "error"].indexOf(name) < 0)
                return false;
            quickSettings.powerAvailable = name !== "unavailable";
            quickSettings.powerProfile = name === "power-saver" ? "power-saver" : ["performance", "degraded", "lap"].indexOf(name) >= 0 ? "performance" : "balanced";
            quickSettings.powerProfiles = name === "limited" ? ["power-saver", "balanced"] : ["power-saver", "balanced", "performance"];
            quickSettings.performanceDegraded = name === "degraded" ? "high-operating-temperature" : name === "lap" ? "lap-detected" : "";
            quickSettings.powerPending = name === "pending";
            quickSettings.powerError = name === "error" ? "Could not change the power profile" : "";
            root.view = "power";
            return true;
        }
        function airplaneState(name: string): bool {
            if (["ready", "on", "bluetooth", "no-wwan", "hardware-blocked", "unavailable", "pending", "error"].indexOf(name) < 0)
                return false;
            airplane.available = name !== "unavailable";
            airplane.pending = false;
            airplane.enabled = false;
            airplane.error = "";
            airplane.restoreAvailable = false;
            airplane.saved = {
                wifi: true,
                bluetooth: true,
                wwan: true
            };
            wifi.available = name !== "unavailable";
            wifi.wirelessEnabled = name !== "hardware-blocked";
            wifi.wirelessHardwareEnabled = name !== "hardware-blocked";
            bluetooth.adapterCount = name === "unavailable" ? 0 : 1;
            bluetoothAdapter.powered = true;
            airplane.wwanAvailable = name !== "no-wwan" && name !== "unavailable";
            airplane.wwanHardwareEnabled = true;
            airplane.wwanEnabled = true;
            if (name === "on" || name === "bluetooth")
                airplane.setEnabled(true);
            if (name === "bluetooth")
                airplane.setBluetoothEnabled(true);
            if (name === "error") {
                wifi.wirelessEnabled = false;
                airplane.wwanEnabled = false;
                airplane.restoreAvailable = true;
                airplane.error = "Some radios could not be changed. Check their states or restore the previous settings.";
            }
            airplane.pending = name === "pending";
            root.view = "airplane";
            return true;
        }
        function colorModeState(name: string): bool {
            if (["ready", "light", "dark", "system-light", "system-dark", "unavailable", "preview", "error"].indexOf(name) < 0)
                return false;
            AppearanceStore.endPreview();
            colorMode.systemAvailable = name !== "unavailable";
            colorMode.systemDark = name !== "system-light";
            colorMode.previewActive = name === "preview";
            colorMode.error = name === "error" ? "Could not save color mode" : "";
            if (name === "preview")
                AppearanceStore.beginPreview();
            colorMode.setMode(name === "light" ? "light" : name === "system-light" || name === "system-dark" ? "system" : "dark");
            root.appearancePage = "";
            root.view = "darkmode";
            return true;
        }
        function wallpaperState(name: string): bool {
            if (["ready", "per-display", "empty", "missing", "preview", "fit", "stretch", "center"].indexOf(name) < 0)
                return false;
            AppearanceStore.endPreview();
            AppearanceStore.setValue("wallpapers", {});
            root.wallpaperScreen = name === "per-display" ? "Virtual-1" : "";
            if (name === "preview")
                AppearanceStore.beginPreview();
            if (name === "missing") {
                AppearanceStore.setValue("wallpapers", {
                    "": {
                        path: root.wallpaperPath("missing-wallpaper.png"),
                        fit: "fill"
                    }
                });
            } else if (name !== "empty") {
                AppearanceStore.setWallpaper(root.wallpaperPath("fixture-spectrum.png"), "", ["fit", "stretch", "center"].indexOf(name) >= 0 ? name : "fill");
                if (name === "per-display")
                    AppearanceStore.setWallpaper(root.wallpaperPath("fixture-ember.png"), "Virtual-1", "fit");
            }
            root.appearancePage = "";
            root.view = "wallpaper";
            return true;
        }
        function batteryState(name: string): bool {
            if (["ready", "charging", "full", "low", "estimating", "unknown-charge", "no-battery", "empty", "unavailable", "pending"].indexOf(name) < 0)
                return false;
            batteryHost.available = name !== "unavailable" && name !== "pending";
            batteryHost.pending = name === "pending";
            batteryHost.onBattery = name !== "charging" && name !== "full";
            laptopBattery.isPresent = name !== "no-battery" && name !== "empty";
            laptopBattery.percentage = name === "low" ? 8 : name === "full" ? 100 : name === "unknown-charge" ? NaN : 82;
            laptopBattery.state = name === "charging" ? 1 : name === "full" ? 4 : 2;
            laptopBattery.timeToEmpty = name === "estimating" ? 0 : name === "low" ? 1200 : 21600;
            laptopBattery.timeToFull = name === "charging" ? 5400 : 0;
            laptopBattery.energyRate = name === "charging" ? 22.4 : name === "full" ? 0 : 8.2;
            laptopBattery.energy = name === "low" ? 4.8 : name === "full" ? 60 : 49.2;
            laptopBattery.iconName = name === "charging" ? "battery-good-charging" : name === "low" ? "battery-caution" : "battery-good";
            batteryHost.displayDevice = laptopBattery.isPresent ? laptopBattery : null;
            batteryDevices.clear();
            if (name !== "empty") {
                if (laptopBattery.isPresent)
                    batteryDevices.append({
                        device: laptopBattery
                    });
                batteryDevices.append({
                    device: headphoneBattery
                });
                batteryDevices.append({
                    device: mouseBattery
                });
            }
            quickSettings.powerAvailable = true;
            quickSettings.powerProfiles = ["power-saver", "balanced", "performance"];
            quickSettings.powerProfile = "balanced";
            quickSettings.powerPending = false;
            quickSettings.powerError = "";
            root.view = "battery";
            return true;
        }
        function viewport(width: int, height: int): bool {
            if (width < 300 || width > 600 || height < 240 || height > 1200)
                return false;
            root.previewWidth = width;
            root.previewHeight = height;
            return true;
        }
        function textScale(percent: int): bool {
            return AppearanceStore.setValue("textScale", percent);
        }
        function preset(name: string): bool {
            return AppearanceStore.applyPreset(name);
        }
    }
}
