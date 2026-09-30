// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Bar

TestCase {
    id: testCase
    name: "QuickServicePanels"
    when: windowShown

    QtObject {
        id: mockController
        property bool nightLightAvailable: true
        property bool nightLightScheduleAvailable: true
        property bool nightLightEnabled: true
        property bool nightLightRunning: true
        property bool nightLightInhibited: false
        property bool nightLightPaused: false
        property int nightLightTemperature: 4500
        property string nightLightSchedule: "automatic"
        property string nightLightMorning: "07:00"
        property string nightLightEvening: "21:00"
        property bool nightLightPending: false
        property string nightLightError: ""
        property bool powerAvailable: true
        property string powerProfile: "balanced"
        property var powerProfiles: ["balanced", "power-saver", "performance"]
        property string performanceDegraded: ""
        property bool powerPending: false
        property string powerError: ""
        property bool available: true
        property bool enabled: false
        property bool pending: false
        property string error: ""
        property bool wifiAvailable: true
        property bool wifiEnabled: true
        property bool wifiHardwareEnabled: true
        property bool bluetoothAvailable: true
        property bool bluetoothEnabled: true
        property bool wwanAvailable: false
        property bool wwanEnabled: false
        property bool wwanHardwareEnabled: true
        property bool restoreAvailable: false
        property string lastAction: ""
        property var lastArguments: []
        function record(action, values) {
            lastAction = action;
            lastArguments = values;
        }
        function refreshNightLight() {
            record("refreshNightLight", []);
        }
        function setNightLightEnabled(value) {
            record("nightLightEnabled", [value]);
        }
        function setNightLightTemperature(value) {
            record("temperature", [value]);
        }
        function setNightLightSchedule(value) {
            record("schedule", [value]);
        }
        function setNightLightTimes(morning, evening) {
            record("times", [morning, evening]);
        }
        function toggleNightLightPause() {
            record("pause", []);
        }
        function refreshPower() {
            record("refreshPower", []);
        }
        function setPowerProfile(value) {
            record("profile", [value]);
        }
        function refresh() {
            record("refreshRadios", []);
        }
        function setEnabled(value) {
            record("airplane", [value]);
        }
        function setWifiEnabled(value) {
            record("wifi", [value]);
        }
        function setBluetoothEnabled(value) {
            record("bluetooth", [value]);
        }
        function setWwanEnabled(value) {
            record("wwan", [value]);
        }
        function restoreRadios() {
            record("restore", []);
        }
    }
    Component {
        id: nightLightComponent
        NightLightPanel {
            controller: mockController
        }
    }
    Component {
        id: powerComponent
        PowerProfilesPanel {
            controller: mockController
        }
    }
    Component {
        id: airplaneComponent
        AirplanePanel {
            controller: mockController
        }
    }

    function init() {
        mockController.nightLightAvailable = true;
        mockController.nightLightScheduleAvailable = true;
        mockController.nightLightEnabled = true;
        mockController.nightLightRunning = true;
        mockController.nightLightInhibited = false;
        mockController.nightLightPaused = false;
        mockController.nightLightTemperature = 4500;
        mockController.nightLightSchedule = "automatic";
        mockController.nightLightMorning = "07:00";
        mockController.nightLightEvening = "21:00";
        mockController.nightLightPending = false;
        mockController.powerAvailable = true;
        mockController.powerProfiles = ["balanced", "power-saver", "performance"];
        mockController.performanceDegraded = "";
        mockController.powerPending = false;
        mockController.available = true;
        mockController.enabled = false;
        mockController.pending = false;
        mockController.wifiHardwareEnabled = true;
        mockController.wwanAvailable = false;
        mockController.restoreAvailable = false;
        mockController.lastAction = "";
        mockController.lastArguments = [];
    }

    function panel(component) {
        const item = createTemporaryObject(component, testCase.parent, {
            width: 410,
            height: 760
        });
        verify(item);
        return item;
    }

    function test_unavailable_night_light_keeps_controls_inactive() {
        mockController.nightLightAvailable = false;
        const item = panel(nightLightComponent);
        compare(findChild(item, "nightLightToggle").enabled, false);
        mockController.nightLightAvailable = true;
        compare(findChild(item, "nightLightToggle").enabled, true);
        mockController.nightLightPending = true;
        compare(findChild(item, "nightLightToggle").enabled, false);
        compare(findChild(item, "nightLightTemperature").enabled, false);
    }

    function test_missing_schedule_service_keeps_manual_control() {
        mockController.nightLightScheduleAvailable = false;
        const item = panel(nightLightComponent);
        compare(findChild(item, "nightLightSchedule-automatic").enabled, false);
        compare(findChild(item, "nightLightSchedule-custom").enabled, false);
        compare(findChild(item, "nightLightSchedule-manual").enabled, true);
    }

    function test_custom_hours_validate_and_preserve_morning_evening_order() {
        mockController.nightLightSchedule = "custom";
        const item = panel(nightLightComponent);
        const start = findChild(item, "nightLightEvening");
        const end = findChild(item, "nightLightMorning");
        const save = findChild(item, "nightLightSaveHours");
        start.text = "07:00";
        compare(save.enabled, false, "equal endpoints cannot form a schedule");
        start.text = "25:00";
        compare(save.enabled, false, "invalid clock values cannot be submitted");
        start.text = "22:15";
        end.text = "06:45";
        compare(save.enabled, true);
        save.clicked();
        compare(mockController.lastAction, "times");
        compare(mockController.lastArguments, ["06:45", "22:15"]);
    }

    function test_temperature_keyboard_uses_the_service() {
        const item = panel(nightLightComponent);
        const slider = findChild(item, "nightLightTemperature");
        slider.forceActiveFocus();
        keyClick(Qt.Key_Left);
        compare(mockController.lastAction, "temperature");
        compare(mockController.lastArguments, [4400]);
    }

    function test_degraded_performance_remains_selectable() {
        mockController.performanceDegraded = "high-operating-temperature";
        const item = panel(powerComponent);
        const performance = findChild(item, "powerProfile-performance");
        verify(performance);
        compare(performance.enabled, true);
        performance.forceActiveFocus();
        keyClick(Qt.Key_Space);
        compare(mockController.lastAction, "profile");
        compare(mockController.lastArguments, ["performance"]);
    }

    function test_profiles_follow_device_capabilities_and_pending_requests() {
        mockController.powerProfiles = ["balanced", "power-saver"];
        const item = panel(powerComponent);
        compare(findChild(item, "powerProfile-performance"), null);
        verify(findChild(item, "powerProfile-power-saver"));
        mockController.powerPending = true;
        compare(findChild(item, "powerProfile-balanced").enabled, false);
        compare(findChild(item, "powerProfile-power-saver").enabled, false);
    }

    function test_airplane_radios_respect_hardware_and_available_devices() {
        mockController.wifiHardwareEnabled = false;
        const item = panel(airplaneComponent);
        compare(findChild(item, "airplaneWifi").enabled, false);
        compare(findChild(item, "airplaneBluetooth").enabled, true);
        compare(findChild(item, "airplaneMobileBroadband").visible, false);
        mockController.wwanAvailable = true;
        compare(findChild(item, "airplaneMobileBroadband").visible, true);
        mockController.pending = true;
        compare(findChild(item, "airplaneToggle").enabled, false);
        compare(findChild(item, "airplaneBluetooth").enabled, false);
    }

    function test_airplane_partial_failure_offers_service_restore() {
        const item = panel(airplaneComponent);
        const restore = findChild(item, "airplaneRestore");
        compare(restore.visible, false);
        mockController.restoreAvailable = true;
        compare(restore.visible, true);
        restore.forceActiveFocus();
        keyClick(Qt.Key_Space);
        compare(mockController.lastAction, "restore");
    }
}
