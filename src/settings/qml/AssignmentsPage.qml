// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // Sub-components expect kcmModule with screens/layouts/settings properties.
    // We create a bridge that combines Settings (kcm) + SettingsController properties.
    readonly property var
    kcmModule: QtObject {
        // Forward all Settings properties that sub-components use
        readonly property bool autotileEnabled: kcm.autotileEnabled
        readonly property string autotileAlgorithm: kcm.autotileAlgorithm
        readonly property string defaultLayoutId: kcm.defaultLayoutId
        // Properties from SettingsController
        readonly property var screens: settingsController.screens
        readonly property var layouts: settingsController.layouts
        // Properties from SettingsController (D-Bus queries)
        property int assignmentViewMode: 0
        readonly property int virtualDesktopCount: settingsController.virtualDesktopCount()
        readonly property var virtualDesktopNames: settingsController.virtualDesktopNames()
        readonly property bool activitiesAvailable: settingsController.activitiesAvailable()
        readonly property var activities: settingsController.activities()
        readonly property string currentActivity: settingsController.currentActivity()
        readonly property var disabledMonitors: kcm.disabledMonitors

        // Signals for sub-component Connections blocks
        signal screenAssignmentsChanged()
        signal tilingScreenAssignmentsChanged()
        signal tilingDesktopAssignmentsChanged()
        signal lockedScreensChanged()
        signal quickLayoutSlotsChanged()
        signal tilingQuickLayoutSlotsChanged()
        signal appRulesRefreshed()
        signal activityAssignmentsChanged()
        signal tilingActivityAssignmentsChanged()

        // Methods
        function isMonitorDisabled(name) {
            return settingsController.isMonitorDisabled(name);
        }

        function setMonitorDisabled(name, disabled) {
            settingsController.setMonitorDisabled(name, disabled);
        }

        function assignLayoutToScreen(screen, layout) {
            settingsController.assignLayoutToScreen(screen, layout);
        }

        function clearScreenAssignment(screen) {
            settingsController.clearScreenAssignment(screen);
        }

        function assignTilingLayoutToScreen(screen, layout) {
            settingsController.assignTilingLayoutToScreen(screen, layout);
        }

        function clearTilingScreenAssignment(screen) {
            settingsController.clearTilingScreenAssignment(screen);
        }

        function getLayoutForScreen(screen) {
            return "";
        }

        function getTilingLayoutForScreen(screen) {
            return "";
        }

        function isScreenLocked(screen, mode) {
            return false;
        }

        function toggleScreenLock(screen, mode) {
        }

        function getQuickLayoutSlot(n) {
            return "";
        }

        function setQuickLayoutSlot(n, id) {
        }

        function getQuickLayoutShortcut(n) {
            return "";
        }

        function getTilingQuickLayoutSlot(n) {
            return "";
        }

        function setTilingQuickLayoutSlot(n, id) {
        }

        function getAppRulesForLayout(id) {
            return [];
        }

        function setAppRulesForLayout(id, rules) {
        }

        function addAppRuleToLayout(id, pattern, zone, screen) {
        }

        function removeAppRuleFromLayout(id, idx) {
        }

        function getRunningWindows() {
            return [];
        }

        function hasExplicitAssignmentForScreenDesktop(screen, desktop) {
            return false;
        }

        function hasExplicitTilingAssignmentForScreenDesktop(screen, desktop) {
            return false;
        }

        function hasExplicitAssignmentForScreenActivity(screen, activity) {
            return false;
        }

        function hasExplicitTilingAssignmentForScreenActivity(screen, activity) {
            return false;
        }

        function isContextLocked(screen, desktop, activity, mode) {
            return false;
        }

        function toggleContextLock(screen, desktop, activity, mode) {
        }

        function getLayoutForScreenDesktop(screen, desktop) {
            return "";
        }

        function getSnappingLayoutForScreenDesktop(screen, desktop) {
            return "";
        }

        function getTilingLayoutForScreenDesktop(screen, desktop) {
            return "";
        }

        function clearScreenDesktopAssignment(screen, desktop) {
        }

        function assignLayoutToScreenDesktop(screen, desktop, layout) {
        }

        function assignTilingLayoutToScreenDesktop(screen, desktop, layout) {
        }

        function clearTilingScreenDesktopAssignment(screen, desktop) {
        }

        function getLayoutForScreenActivity(screen, activity) {
            return "";
        }

        function getSnappingLayoutForScreenActivity(screen, activity) {
            return "";
        }

        function getTilingLayoutForScreenActivity(screen, activity) {
            return "";
        }

        function clearScreenActivityAssignment(screen, activity) {
        }

        function assignLayoutToScreenActivity(screen, activity, layout) {
        }

        function assignTilingLayoutToScreenActivity(screen, activity, layout) {
        }

        function clearTilingScreenActivityAssignment(screen, activity) {
        }

    }
    // View mode: 0 = snapping (zone layouts), 1 = tiling (autotile algorithms)

    readonly property int viewMode: kcm.autotileEnabled ? root.kcmModule.assignmentViewMode : 0

    contentHeight: mainCol.implicitHeight
    clip: true

    // Inline constants (from monolith Constants object)
    QtObject {
        id: constants

        readonly property real labelSecondaryOpacity: 0.7
        readonly property int quickLayoutSlotCount: 9
    }

    WindowPickerDialog {
        id: windowPickerDialog

        kcm: root.kcmModule
    }

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Assign layouts to monitors, activities, configure quick-switch shortcuts, and set up app-to-zone rules.")
            visible: true
        }

        // Mode selector (visible only when autotiling is enabled)
        Item {
            Layout.fillWidth: true
            implicitHeight: modeSelectorCard.implicitHeight
            visible: root.kcmModule.autotileEnabled

            Kirigami.Card {
                id: modeSelectorCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Configuration Mode")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    WideComboBox {
                        id: modeCombo

                        Layout.fillWidth: true
                        model: [{
                            "text": i18n("Snapping — Zone layouts"),
                            "value": 0
                        }, {
                            "text": i18n("Tiling — Autotile algorithms"),
                            "value": 1
                        }]
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: root.kcmModule.assignmentViewMode
                        onActivated: {
                            root.kcmModule.assignmentViewMode = model[currentIndex].value;
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.text: i18n("Switch between snapping and tiling configurations. Both are saved independently.")
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.smallSpacing
                        Layout.topMargin: Kirigami.Units.smallSpacing * 2
                        visible: true
                        type: root.viewMode === 1 ? Kirigami.MessageType.Positive : Kirigami.MessageType.Information
                        text: root.viewMode === 1 ? i18n("Tiling mode: assign autotile algorithms to each monitor. These are used when tiling is active.") : i18n("Snapping mode: assign zone layouts to each monitor. These are used when dragging windows.")
                    }

                }

            }

        }

        // Monitor Assignments
        Item {
            Layout.fillWidth: true
            implicitHeight: monitorCard.implicitHeight

            MonitorAssignmentsCard {
                id: monitorCard

                anchors.fill: parent
                kcm: root.kcmModule
                constants: constants
                viewMode: root.viewMode
            }

        }

        // Activity Assignments
        Item {
            Layout.fillWidth: true
            implicitHeight: activityCard.implicitHeight
            visible: root.kcmModule.activitiesAvailable

            ActivityAssignmentsCard {
                id: activityCard

                anchors.fill: parent
                kcm: root.kcmModule
                constants: constants
                viewMode: root.viewMode
            }

        }

        // Info when Activities not available
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: !root.kcmModule.activitiesAvailable && root.kcmModule.screens.length > 0
            type: Kirigami.MessageType.Information
            text: i18n("KDE Activities support is not available. Activity-based layout assignments require the KDE Activities service to be running.")
        }

        // App-to-zone rules
        Item {
            Layout.fillWidth: true
            implicitHeight: appRulesCard.implicitHeight

            AppRulesCard {
                id: appRulesCard

                anchors.fill: parent
                kcm: root.kcmModule
                constants: constants
                windowPickerDialog: windowPickerDialog
                viewMode: root.viewMode
            }

        }

        // Quick Layout Shortcuts
        Item {
            Layout.fillWidth: true
            implicitHeight: quickSlotsCard.implicitHeight

            QuickLayoutSlotsCard {
                id: quickSlotsCard

                anchors.fill: parent
                kcm: root.kcmModule
                constants: constants
                viewMode: root.viewMode
            }

        }

    }

}
