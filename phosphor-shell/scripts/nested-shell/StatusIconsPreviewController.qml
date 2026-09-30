// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Hardware-free controller for the nested status-icon preview.
import QtQuick
import Phosphor.Theme

QtObject {
    id: root
    required property var network
    required property var networkAdapter
    required property var bluetoothHost
    required property var bluetoothRadio
    required property var battery
    required property var batteryDevice
    required property var quickController
    required property var airplaneController
    property string scenario: "everyday"
    property int volume: 64
    property bool muted: false
    property bool audioAvailable: true
    property bool microphoneInUse: false
    property bool microphoneMuted: false
    property bool focusEnabled: false
    property string selectedOutput: "speakers"
    property string error: ""
    readonly property var ids: ["wifi", "audio", "bluetooth", "battery", "microphone", "nightlight", "focus", "airplane", "power"]
    readonly property int maxVisible: AppearanceStore.values.statusLimit
    readonly property bool showBatteryPercentage: AppearanceStore.values.statusBatteryPercent
    readonly property string powerProfile: quickController.powerProfile
    readonly property var powerProfiles: quickController.powerProfiles
    readonly property var outputs: audioAvailable ? [
        {
            id: "speakers",
            name: i18n("Built-in speakers"),
            selected: selectedOutput === "speakers"
        },
        {
            id: "headphones",
            name: i18n("Headphones"),
            selected: selectedOutput === "headphones"
        }
    ] : []
    readonly property var catalog: ({
            wifi: [i18n("Wi-Fi"), i18n("Your wireless connection"), i18n("When connected"), "network-wireless", "network"],
            audio: [i18n("Sound"), i18n("Volume and output device"), i18n("When muted"), "audio-volume-high", "audio"],
            bluetooth: [i18n("Bluetooth"), i18n("Your connected devices"), i18n("When connected"), "network-bluetooth-symbolic", "bluetooth"],
            battery: [i18n("Battery"), i18n("Charge and time remaining"), i18n("When present"), "battery-good-symbolic", "battery"],
            microphone: [i18n("Microphone"), i18n("Input activity and mute"), i18n("When in use"), "audio-input-microphone", "microphone"],
            nightlight: [i18n("Night light"), i18n("Warmer display colors"), i18n("When on"), "weather-clear-night-symbolic", "nightlight"],
            focus: [i18n("Do not disturb"), i18n("Keep notifications quiet"), i18n("When on"), "notifications-disabled", "notification"],
            airplane: [i18n("Airplane mode"), i18n("Wireless radio state"), i18n("When on"), "flightmode-on", "airplane"],
            power: [i18n("Power profile"), i18n("Performance and energy use"), i18n("When not balanced"), "speedometer", "power"]
        })
    readonly property var icons: AppearanceStore.values.statusOrder.map(id => root.entry(id))
    readonly property var eligibleIcons: icons.filter(item => item.visibility === "always" || item.visibility === "auto" && item.active)
    readonly property var shownIcons: eligibleIcons.slice(0, maxVisible)
    readonly property var overflowIcons: eligibleIcons.slice(maxVisible)

    function entry(id: string): var {
        const metadata = catalog[id];
        const row = {
            id: id,
            title: metadata[0],
            description: metadata[1],
            condition: metadata[2],
            iconName: metadata[3],
            detailId: metadata[4],
            visibility: AppearanceStore.values.statusVisibility[id],
            summary: "",
            active: false,
            off: false,
            available: true,
            attention: false,
            percent: -1,
            pending: scenario === "pending",
            error: scenario === "error" ? i18n("The service could not apply this change.") : ""
        };
        if (id === "wifi") {
            row.available = network.available;
            row.off = !network.wirelessEnabled;
            row.active = row.available && !row.off && networkAdapter.state === 100;
            row.attention = row.active && network.connectivity === 2;
            row.summary = row.off ? i18n("Wi-Fi off") : row.attention ? i18n("Sign in to the network") : row.active ? i18n("Home network") : i18n("Not connected");
        } else if (id === "audio") {
            row.available = audioAvailable;
            row.off = muted;
            row.active = row.available && muted;
            row.iconName = muted ? "audio-volume-muted" : "audio-volume-high";
            row.summary = muted ? i18n("Muted") : i18n("%1%, %2").arg(volume).arg(selectedOutput === "speakers" ? i18n("Built-in speakers") : i18n("Headphones"));
        } else if (id === "bluetooth") {
            row.available = bluetoothHost.adapterCount > 0;
            row.off = !bluetoothRadio.powered;
            row.active = row.available && !row.off;
            row.summary = row.off ? i18n("Bluetooth off") : i18n("Headphones, MX Master");
        } else if (id === "battery") {
            row.available = battery.available;
            row.active = row.available && batteryDevice.isPresent;
            row.off = !row.active;
            row.percent = row.active && Number.isFinite(batteryDevice.percentage) ? Math.round(batteryDevice.percentage) : -1;
            row.attention = row.percent >= 0 && row.percent <= 15 && batteryDevice.state === 2;
            row.iconName = batteryDevice.iconName;
            row.pending = row.pending || battery.pending;
            row.summary = !row.active ? i18n("No system battery") : row.percent < 0 ? i18n("Charge unavailable") : batteryDevice.state === 1 ? i18n("%1%, charging").arg(row.percent) : i18n("%1%, on battery").arg(row.percent);
        } else if (id === "microphone") {
            row.available = audioAvailable;
            row.active = microphoneInUse;
            row.attention = row.active;
            row.off = microphoneMuted;
            row.iconName = row.off ? "microphone-sensitivity-muted" : "audio-input-microphone";
            row.summary = row.off ? i18n("Microphone muted") : row.active ? i18n("In use by Video call") : i18n("Microphone idle");
        } else if (id === "nightlight") {
            row.available = quickController.nightLightAvailable;
            row.active = row.available && quickController.nightLightRunning;
            row.off = !quickController.nightLightEnabled || quickController.nightLightInhibited;
            row.pending = row.pending || quickController.nightLightPending;
            row.error = row.error || quickController.nightLightError;
            row.summary = row.off ? i18n("Night light off") : row.active ? i18n("Night light on") : i18n("Night light scheduled");
        } else if (id === "focus") {
            row.active = focusEnabled;
            row.off = !row.active;
            row.summary = row.active ? i18n("Do not disturb on") : i18n("Do not disturb off");
        } else if (id === "airplane") {
            row.available = airplaneController.available;
            row.active = airplaneController.enabled;
            row.off = !row.active;
            row.pending = row.pending || airplaneController.pending;
            row.error = row.error || airplaneController.error;
            row.summary = row.active ? i18n("Airplane mode on") : i18n("Airplane mode off");
        } else if (id === "power") {
            row.available = quickController.powerAvailable;
            row.active = row.available && powerProfile !== "balanced";
            row.pending = row.pending || quickController.powerPending;
            row.error = row.error || quickController.powerError;
            row.summary = powerProfile === "power-saver" ? i18n("Power saver") : powerProfile === "performance" ? i18n("Performance") : i18n("Balanced");
        }
        if (scenario === "unavailable") {
            row.available = false;
            row.active = false;
        }
        if (!row.available)
            row.summary = i18n("Unavailable");
        return row;
    }
    function save(key: string, value: var): bool {
        const saved = AppearanceStore.setValue(key, value);
        error = saved ? "" : AppearanceStore.error;
        return saved;
    }
    function setVisibility(id: string, policy: string): bool {
        if (!ids.includes(id) || !["always", "auto", "hidden"].includes(policy))
            return false;
        const policies = Object.assign({}, AppearanceStore.values.statusVisibility);
        policies[id] = policy;
        return save("statusVisibility", policies);
    }
    function moveIcon(id: string, delta: int): bool {
        const order = AppearanceStore.values.statusOrder.slice();
        const from = order.indexOf(id);
        if (![-1, 1].includes(delta) || from < 0 || from + delta < 0 || from + delta >= order.length)
            return false;
        order.splice(from, 1);
        order.splice(from + delta, 0, id);
        return save("statusOrder", order);
    }
    function moveBefore(id: string, beforeId: string): bool {
        if (!ids.includes(id) || beforeId !== "" && !ids.includes(beforeId) || id === beforeId)
            return false;
        const order = AppearanceStore.values.statusOrder.filter(value => value !== id);
        order.splice(beforeId === "" ? order.length : order.indexOf(beforeId), 0, id);
        return save("statusOrder", order);
    }
    function setMaxVisible(value: int): bool {
        return save("statusLimit", value);
    }
    function setShowBatteryPercentage(value: bool): bool {
        return save("statusBatteryPercent", value);
    }
    function resetIcons(): bool {
        const defaults = {
            statusOrder: ids,
            statusVisibility: {
                wifi: "always",
                audio: "always",
                bluetooth: "hidden",
                battery: "auto",
                microphone: "auto",
                nightlight: "hidden",
                focus: "auto",
                airplane: "auto",
                power: "hidden"
            },
            statusLimit: 4,
            statusBatteryPercent: true
        };
        const saved = AppearanceStore.setValues(Object.assign({}, AppearanceStore.values, defaults));
        error = saved ? "" : AppearanceStore.error;
        return saved;
    }
    function toggle(id: string): bool {
        const row = icons.find(item => item.id === id);
        if (!row || !row.available || row.pending)
            return false;
        if (id === "wifi")
            airplaneController.setWifiEnabled(!network.wirelessEnabled);
        else if (id === "audio")
            muted = !muted;
        else if (id === "microphone")
            microphoneMuted = !microphoneMuted;
        else if (id === "bluetooth")
            airplaneController.setBluetoothEnabled(!bluetoothRadio.powered);
        else if (id === "nightlight")
            quickController.setNightLightEnabled(!quickController.nightLightEnabled);
        else if (id === "focus")
            focusEnabled = !focusEnabled;
        else if (id === "airplane")
            airplaneController.setEnabled(!airplaneController.enabled);
        else
            return false;
        return true;
    }
    function setVolume(value: int): bool {
        if (!audioAvailable || scenario === "pending" || value < 0 || value > 100)
            return false;
        volume = value;
        return true;
    }
    function selectOutput(id: string): bool {
        if (!audioAvailable || scenario === "pending" || !outputs.some(item => item.id === id))
            return false;
        selectedOutput = id;
        return true;
    }
    function setPowerProfile(id: string): bool {
        if (!quickController.powerAvailable || quickController.powerPending || scenario === "pending" || !powerProfiles.includes(id))
            return false;
        quickController.setPowerProfile(id);
        return true;
    }
    function setScenario(name: string): bool {
        if (!["everyday", "meeting", "travel", "desktop", "all", "empty", "unavailable", "pending", "error"].includes(name))
            return false;
        scenario = name;
        resetIcons();
        audioAvailable = name !== "unavailable";
        volume = 64;
        muted = false;
        microphoneInUse = name === "meeting" || name === "all";
        microphoneMuted = false;
        focusEnabled = name === "meeting" || name === "all";
        selectedOutput = name === "meeting" ? "headphones" : "speakers";
        network.available = name !== "unavailable";
        network.wirelessHardwareEnabled = true;
        network.wirelessEnabled = true;
        network.connectivity = 4;
        networkAdapter.state = 100;
        bluetoothHost.adapterCount = name === "unavailable" ? 0 : 1;
        bluetoothRadio.powered = true;
        battery.available = name !== "unavailable";
        battery.pending = false;
        batteryDevice.isPresent = name !== "desktop";
        battery.displayDevice = batteryDevice.isPresent ? batteryDevice : null;
        batteryDevice.percentage = name === "travel" ? 12 : 82;
        batteryDevice.state = 2;
        batteryDevice.timeToEmpty = name === "travel" ? 1200 : 21600;
        batteryDevice.timeToFull = 0;
        batteryDevice.iconName = name === "travel" ? "battery-caution" : "battery-good";
        quickController.nightLightAvailable = name !== "unavailable";
        quickController.nightLightEnabled = name === "all";
        quickController.nightLightRunning = name === "all";
        quickController.nightLightInhibited = false;
        quickController.nightLightPaused = false;
        quickController.nightLightPending = false;
        quickController.nightLightError = "";
        quickController.powerAvailable = name !== "unavailable";
        quickController.powerPending = false;
        quickController.powerError = "";
        quickController.performanceDegraded = "";
        quickController.powerProfile = name === "travel" ? "power-saver" : "balanced";
        quickController.powerProfiles = ["power-saver", "balanced", "performance"];
        airplaneController.available = name !== "unavailable";
        airplaneController.pending = false;
        airplaneController.error = "";
        airplaneController.enabled = false;
        airplaneController.restoreAvailable = false;
        airplaneController.wwanAvailable = name !== "unavailable";
        airplaneController.wwanHardwareEnabled = true;
        airplaneController.wwanEnabled = true;
        if (name === "travel") {
            airplaneController.setEnabled(true);
            setVisibility("power", "auto");
        }
        if (["all", "empty", "unavailable", "pending", "error"].includes(name)) {
            const policies = {};
            for (const id of ids)
                policies[id] = name === "empty" ? "hidden" : "always";
            save("statusVisibility", policies);
        }
        return true;
    }
}
