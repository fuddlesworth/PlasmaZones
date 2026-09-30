// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Bar
import Phosphor.Theme
import Phosphor.Service.UPower

TestCase {
    id: testCase
    name: "BatteryAndAppearanceDetails"
    when: windowShown
    QtObject {
        id: battery
        property bool isPresent: true
        property real percentage: 82
        property int state: UPowerDevice.Discharging
        property real timeToEmpty: 21600
        property real timeToFull: 0
        property real healthPercentage: 92
        property real energyRate: 8.2
        property real energy: 41
        property real energyCapacity: 50
        property string iconName: "battery"
    }
    QtObject {
        id: powerHost
        property bool available: true
        property bool pending: false
        property bool onBattery: true
        property var displayDevice: battery
        function refresh() {
        }
    }
    QtObject {
        id: mockController
        property bool powerAvailable: true
        property string powerProfile: "balanced"
        property var powerProfiles: ["balanced", "power-saver"]
        property bool powerPending: false
        property string powerError: ""
        property string mode: "dark"
        property bool systemAvailable: true
        property bool systemDark: false
        property bool effectiveDark: true
        property bool previewActive: false
        property string error: ""
        property string lastAction: ""
        function setPowerProfile(value) {
            lastAction = value;
        }
        function setMode(value) {
            mode = value;
            lastAction = value;
        }
        function refresh() {
            lastAction = "refresh";
        }
    }
    ListModel {
        id: emptyDevices
    }
    Component {
        id: batteryComponent
        BatteryPanel {
            serviceHost: powerHost
            deviceModel: emptyDevices
            controller: mockController
        }
    }
    Component {
        id: darkComponent
        DarkModePanel {
            controller: mockController
        }
    }
    Component {
        id: wallpaperComponent
        WallpaperPanel {}
    }
    Component {
        id: spyComponent
        SignalSpy {}
    }
    function panel(component, values) {
        const item = createTemporaryObject(component, testCase.parent, Object.assign({
            width: 410,
            height: 760
        }, values || {}));
        verify(item);
        return item;
    }
    function init() {
        battery.isPresent = true;
        battery.percentage = 82;
        battery.state = UPowerDevice.Discharging;
        battery.timeToEmpty = 21600;
        battery.timeToFull = 0;
        powerHost.available = true;
        powerHost.pending = false;
        mockController.powerAvailable = true;
        mockController.powerPending = false;
        mockController.powerProfile = "balanced";
        mockController.powerProfiles = ["balanced", "power-saver"];
        mockController.mode = "dark";
        mockController.systemAvailable = true;
        mockController.lastAction = "";
    }
    function test_battery_service_and_desktop_states_are_distinct() {
        const item = panel(batteryComponent);
        compare(item.resolvedPowerService, powerHost);
        verify(findChild(item, "batteryCharge").visible);
        battery.isPresent = false;
        verify(findChild(item, "batteryAbsent").visible);
        verify(!findChild(item, "batteryCharge").visible);
        powerHost.available = false;
        verify(findChild(item, "batteryUnavailable").visible);
        verify(!findChild(item, "batteryAbsent").visible);
        verify(!findChild(item, "batteryCharge").visible);
    }
    function test_estimates_and_full_charge_do_not_invent_time() {
        const item = panel(batteryComponent);
        const charge = findChild(item, "batteryCharge");
        verify(charge.description.indexOf("6 h") >= 0);
        battery.timeToEmpty = 0;
        verify(charge.description.indexOf("Calculating") >= 0);
        battery.state = UPowerDevice.Charging;
        verify(charge.description.indexOf("Charging") >= 0);
        battery.timeToFull = 1800;
        verify(charge.description.indexOf("30 min") >= 0);
        battery.state = UPowerDevice.FullyCharged;
        compare(charge.description, "Fully charged");
    }
    function test_power_saver_requires_supported_service_and_low_battery() {
        const item = panel(batteryComponent);
        const button = findChild(item, "batteryPowerSaver");
        verify(!button.visible);
        battery.percentage = 8;
        verify(button.visible);
        verify(button.enabled);
        button.clicked();
        compare(mockController.lastAction, "power-saver");
        mockController.powerPending = true;
        verify(!button.enabled);
        mockController.powerAvailable = false;
        verify(!button.visible);
        mockController.powerAvailable = true;
        mockController.powerProfiles = ["balanced"];
        verify(!button.visible);
    }
    function test_battery_links_to_power_profile_detail() {
        const item = panel(batteryComponent);
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: item,
            signalName: "panelRequested"
        });
        findChild(item, "batteryPowerProfile").clicked();
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "power");
    }
    function test_dark_mode_keyboard_choices_skip_unavailable_system() {
        const item = panel(darkComponent);
        const dark = findChild(item, "colorMode_dark");
        const system = findChild(item, "colorMode_system");
        dark.forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(mockController.lastAction, "system");
        verify(system.activeFocus);
        mockController.systemDark = !mockController.systemDark;
        compare(findChild(item, "colorMode_system"), system);
        verify(system.activeFocus);
        mockController.systemAvailable = false;
        verify(!system.enabled);
        dark.forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(mockController.lastAction, "light");
    }
    function test_wallpaper_reads_current_output_and_opens_shared_workspace() {
        const before = AppearanceStore.values;
        const sharedPath = decodeURIComponent(String(Qt.resolvedUrl("../../phosphor-shell-picker/wallpapers/linen.svg")).slice(7));
        const outputPath = decodeURIComponent(String(Qt.resolvedUrl("../../phosphor-shell-picker/wallpapers/graphite.svg")).slice(7));
        const next = Object.assign({}, before, {
            wallpapers: {
                "": {
                    path: sharedPath,
                    fit: "fit"
                },
                "DP-1": {
                    path: outputPath,
                    fit: "fill"
                }
            }
        });
        verify(AppearanceStore.beginPreview());
        verify(AppearanceStore.setValues(next));
        const item = panel(wallpaperComponent, {
            screenName: "DP-1"
        });
        compare(item.imagePath, outputPath);
        compare(item.fit, "fill");
        verify(String(item.imageUrl).startsWith("file:///"));
        item.screenName = "DP-2";
        compare(item.imagePath, sharedPath);
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: item,
            signalName: "appearanceRequested"
        });
        findChild(item, "chooseWallpaper").clicked();
        compare(spy.signalArguments[0][0], "wallpaper");
        AppearanceStore.endPreview();
        compare(AppearanceStore.values, before);
    }
    function cleanup() {
        if (AppearanceStore.editing)
            AppearanceStore.endPreview();
    }
}
