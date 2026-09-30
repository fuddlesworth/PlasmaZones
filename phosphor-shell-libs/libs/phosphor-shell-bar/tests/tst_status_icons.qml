// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Bar

Item {
    id: root
    width: 1000
    height: 900
    QtObject {
        id: service
        property var icons: []
        property var shownIcons: []
        property var overflowIcons: []
        property int maxVisible: 4
        property bool showBatteryPercentage: true
        property int volume: 64
        property bool muted: false
        property bool audioAvailable: true
        property string error: ""
        property var outputs: [
            {
                id: "speakers",
                name: "Speakers",
                selected: true
            },
            {
                id: "headphones",
                name: "Headphones",
                selected: false
            }
        ]
        property string powerProfile: "balanced"
        property var powerProfiles: ["power-saver", "balanced", "performance"]
        property string lastAction: ""
        property var lastArguments: []
        function record(action, values) {
            lastAction = action;
            lastArguments = values;
        }
        function setVisibility(id, policy) {
            record("visibility", [id, policy]);
        }
        function moveIcon(id, delta) {
            record("move", [id, delta]);
        }
        function moveBefore(id, beforeId) {
            record("moveBefore", [id, beforeId]);
        }
        function resetIcons() {
            record("reset", []);
        }
        function setMaxVisible(value) {
            record("limit", [value]);
        }
        function setShowBatteryPercentage(value) {
            record("percentage", [value]);
        }
        function toggle(id) {
            record("toggle", [id]);
        }
        function setVolume(value) {
            record("volume", [value]);
        }
        function selectOutput(id) {
            record("output", [id]);
        }
        function setPowerProfile(id) {
            record("profile", [id]);
        }
    }
    Component {
        id: widgetComponent
        ControlCenterButton {
            controller: service
            width: implicitWidth
            height: implicitHeight
            property int activations: 0
            onActivated: activations++
        }
    }
    Component {
        id: panelComponent
        StatusIconsPanel {
            controller: service
            width: implicitWidth
            height: implicitHeight
            property int closes: 0
            property string requestedDetail: "unset"
            onCloseRequested: closes++
            onDetailRequested: panelId => requestedDetail = panelId
        }
    }
    TestCase {
        id: tests
        name: "StatusIcons"
        when: windowShown
        function init() {
            const ids = ["wifi", "audio", "bluetooth", "battery", "microphone", "nightlight", "focus", "airplane", "power"];
            service.icons = ids.map(id => ({
                        id,
                        title: id,
                        description: "Indicator details",
                        condition: "When active",
                        iconName: "configure",
                        summary: "Available",
                        active: true,
                        off: false,
                        available: true,
                        attention: false,
                        percent: id === "battery" ? 82 : -1,
                        pending: false,
                        error: "",
                        detailId: id === "wifi" ? "network" : id,
                        visibility: "always"
                    }));
            service.shownIcons = service.icons.slice(0, 4);
            service.overflowIcons = service.icons.slice(4);
            service.maxVisible = 4;
            service.showBatteryPercentage = true;
            service.volume = 64;
            service.muted = false;
            service.audioAvailable = true;
            service.error = "";
            service.powerProfiles = ["power-saver", "balanced", "performance"];
            service.lastAction = "";
            service.lastArguments = [];
        }
        function control(item, name) {
            const found = findChild(item, name);
            verify(found !== null, name + " exists");
            return found;
        }
        function panel(page) {
            const item = createTemporaryObject(panelComponent, root, {
                page: page
            });
            verify(item);
            verify(waitForRendering(item));
            return item;
        }
        function widget() {
            const item = createTemporaryObject(widgetComponent, root);
            verify(item);
            verify(waitForRendering(item));
            return item;
        }
        function test_primaryAndContextActionsKeepTheirAnchors() {
            const item = widget();
            const wifi = control(item, "statusIcon-wifi");
            mouseClick(wifi);
            compare(item.activations, 1);
            compare(item.requestedStatusPage, "");
            compare(item.requestAnchor, wifi);
            mouseClick(wifi, wifi.width / 2, wifi.height / 2, Qt.RightButton);
            compare(item.activations, 2);
            compare(item.requestedStatusPage, "wifi");
            compare(item.requestAnchor, wifi);
            wifi.forceActiveFocus();
            keyClick(Qt.Key_F10, Qt.ShiftModifier);
            compare(item.activations, 3);
            compare(item.requestedStatusPage, "wifi");
            keyClick(Qt.Key_Right);
            const audio = control(item, "statusIcon-audio");
            tryCompare(audio, "activeFocus", true);
            mouseClick(audio, audio.width / 2, audio.height / 2, Qt.RightButton);
            compare(item.activations, 4);
            compare(item.requestedStatusPage, "audio");
            compare(item.requestAnchor, audio);
            mouseClick(item, 1, 1, Qt.RightButton);
            compare(item.activations, 5);
            compare(item.requestedStatusPage, "settings");
            compare(item.requestAnchor, item);
        }
        function test_overflowAndAllHiddenFallbackStayReachable() {
            const item = widget();
            const overflow = control(item, "statusOverflow");
            verify(overflow.Accessible.name.includes(overflow.activationDescription));
            compare(overflow.activationDescription, i18n("Show more status icons"));
            mouseClick(overflow);
            compare(item.requestedStatusPage, "overflow");
            service.shownIcons = [];
            service.overflowIcons = [];
            const fallback = control(item, "statusFallback");
            tryCompare(fallback, "visible", true);
            verify(waitForRendering(item));
            verify(fallback.x + fallback.width <= item.width);
            mouseClick(fallback);
            compare(item.requestedStatusPage, "");
            fallback.forceActiveFocus();
            keyClick(Qt.Key_F10, Qt.ShiftModifier);
            compare(item.requestedStatusPage, "settings");
            mouseClick(fallback, fallback.width / 2, fallback.height / 2, Qt.RightButton);
            compare(item.activations, 4);
            compare(item.requestedStatusPage, "settings");
            verify(item.implicitWidth > 0);
        }
        function test_batteryPercentageAndExpandedIconFollowHostState() {
            const widget = createTemporaryObject(widgetComponent, root);
            const battery = control(widget, "statusIcon-battery");
            compare(battery.percentageVisible, true);
            service.showBatteryPercentage = false;
            compare(battery.percentageVisible, false);
            widget.expandedStatusPage = "audio";
            compare(control(widget, "statusIcon-audio").expanded, true);
            compare(battery.expanded, false);
            widget.quickSettingsOpen = true;
            compare(battery.expanded, true);
        }
        function test_menuControlsDispatchAndKeepConfirmedState() {
            const item = panel("audio");
            const mute = control(item, "statusToggle-audio");
            mouseClick(mute);
            compare(service.lastAction, "toggle");
            compare(service.lastArguments, ["audio"]);
            compare(mute.selected, false, "selection waits for the service update");
            mouseClick(control(item, "statusOutput-headphones"));
            compare(service.lastAction, "output");
            compare(service.lastArguments, ["headphones"]);
            const slider = control(item, "statusVolume");
            slider.forceActiveFocus();
            keyClick(Qt.Key_Right);
            compare(service.lastAction, "volume");
            compare(service.lastArguments, [65]);
            mouseClick(control(item, "statusDetails"));
            compare(item.requestedDetail, "audio");
        }
        function test_audioFocusDoesNotChangePage() {
            const item = panel("audio");
            tryCompare(control(item, "statusVolume"), "activeFocus", true);
            compare(item.page, "audio");
            keyClick(Qt.Key_Tab);
            compare(item.page, "audio");
            keyClick(Qt.Key_Space);
            compare(item.page, "audio");
            compare(service.lastAction, "toggle");
            compare(service.lastArguments, ["audio"]);
        }
        function test_unavailableMenuStillOffersDetails() {
            service.icons = service.icons.map(entry => entry.id === "wifi" ? Object.assign({}, entry, {
                    available: false,
                    error: "Radio unavailable"
                }) : entry);
            const item = panel("wifi");
            const toggle = control(item, "statusToggle-wifi");
            compare(toggle.enabled, false);
            mouseClick(toggle);
            compare(service.lastAction, "");
            mouseClick(control(item, "statusDetails"));
            compare(item.requestedDetail, "network");
            keyClick(Qt.Key_Escape);
            compare(item.closes, 1);
        }
        function test_pendingVolumeConfirmationDoesNotCancelDragging() {
            const item = panel("audio");
            const slider = control(item, "statusVolume");
            mousePress(slider, slider.width / 2, slider.height / 2);
            compare(slider.pressed, true);
            service.icons = service.icons.map(entry => entry.id === "audio" ? Object.assign({}, entry, {
                    pending: true
                }) : entry);
            compare(slider.enabled, true);
            compare(slider.pressed, true);
            mouseMove(slider, slider.width * 0.8, slider.height / 2);
            mouseRelease(slider, slider.width * 0.8, slider.height / 2);
            compare(service.lastAction, "volume");
            verify(service.lastArguments[0] > 65);
            compare(slider.pressed, false);
        }
        function test_powerMenuUsesOnlySupportedProfiles() {
            service.powerProfiles = ["balanced", "power-saver"];
            const item = panel("power");
            compare(findChild(item, "statusProfile-performance"), null);
            mouseClick(control(item, "statusProfile-power-saver"));
            compare(service.lastAction, "profile");
            compare(service.lastArguments, ["power-saver"]);
        }
        function test_overflowCanOpenMenuAndReturn() {
            const item = panel("overflow");
            mouseClick(control(item, "statusOverflow-power"));
            tryCompare(item, "page", "power");
            mouseClick(control(item, "statusArrange"));
            tryCompare(item, "page", "settings");
            item.back();
            tryCompare(item, "page", "power");
            item.back();
            tryCompare(item, "page", "overflow");
            tryCompare(control(item, "statusOverflow-power"), "activeFocus", true);
        }
        function test_settingsDispatchPreferencesAndBoundLongContent() {
            const item = panel("settings");
            const limit = control(item, "statusLimit");
            limit.forceActiveFocus();
            keyClick(Qt.Key_End);
            compare(service.lastAction, "limit");
            compare(service.lastArguments, [6]);
            mouseClick(control(item, "statusBatteryPercentage"));
            compare(service.lastAction, "percentage");
            compare(service.lastArguments, [false]);
            const policy = control(item, "statusVisibility-wifi");
            policy.forceActiveFocus();
            keyClick(Qt.Key_End);
            compare(service.lastAction, "visibility");
            compare(service.lastArguments, ["wifi", "hidden"]);
            mouseClick(control(item, "statusLater-wifi"));
            compare(service.lastAction, "move");
            compare(service.lastArguments, ["wifi", 1]);
            item.maximumHeight = 420;
            const scroll = control(item, "statusScroll");
            tryVerify(() => scroll.contentHeight > scroll.height);
            verify(item.implicitHeight <= 420);
            control(item, "statusVisibility-power").forceActiveFocus();
            tryVerify(() => scroll.contentY > 0);
            mouseClick(control(item, "statusReset"));
            compare(service.lastAction, "reset");
            mouseClick(control(item, "statusDone"));
            compare(item.closes, 1);
        }
        function test_settingsSaveErrorRemainsVisibleUntilServiceRecovery() {
            const item = panel("settings");
            const notice = control(item, "statusSettingsError");
            compare(notice.visible, false);
            service.error = "The settings could not be saved.";
            compare(notice.visible, true);
            compare(notice.text, service.error);
            service.error = "";
            compare(notice.visible, false);
        }
    }
}
