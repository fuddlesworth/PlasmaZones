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
        readonly property int virtualDesktopCount: settingsController.virtualDesktopCount
        readonly property var virtualDesktopNames: settingsController.virtualDesktopNames
        readonly property bool activitiesAvailable: settingsController.activitiesAvailable
        readonly property var activities: settingsController.activities
        readonly property string currentActivity: settingsController.currentActivity
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
            screenAssignmentsChanged();
        }

        function clearScreenAssignment(screen) {
            settingsController.clearScreenAssignment(screen);
            screenAssignmentsChanged();
        }

        function assignTilingLayoutToScreen(screen, layout) {
            settingsController.assignTilingLayoutToScreen(screen, layout);
            tilingScreenAssignmentsChanged();
        }

        function clearTilingScreenAssignment(screen) {
            settingsController.clearTilingScreenAssignment(screen);
            tilingScreenAssignmentsChanged();
        }

        function getLayoutForScreen(screen) {
            return settingsController.getLayoutForScreen(screen);
        }

        function getTilingLayoutForScreen(screen) {
            return settingsController.getTilingLayoutForScreen(screen);
        }

        function isScreenLocked(screen, mode) {
            return settingsController.isScreenLocked(screen, mode);
        }

        function toggleScreenLock(screen, mode) {
            settingsController.toggleScreenLock(screen, mode);
            lockedScreensChanged();
        }

        function isContextLocked(screen, desktop, activity, mode) {
            return settingsController.isContextLocked(screen, desktop, activity, mode);
        }

        function toggleContextLock(screen, desktop, activity, mode) {
            settingsController.toggleContextLock(screen, desktop, activity, mode);
            lockedScreensChanged();
        }

        // Quick layout slots
        function getQuickLayoutSlot(n) {
            return settingsController.getQuickLayoutSlot(n);
        }

        function setQuickLayoutSlot(n, id) {
            settingsController.setQuickLayoutSlot(n, id);
            quickLayoutSlotsChanged();
        }

        function getQuickLayoutShortcut(n) {
            return settingsController.getQuickLayoutShortcut(n);
        }

        function getTilingQuickLayoutSlot(n) {
            return settingsController.getTilingQuickLayoutSlot(n);
        }

        function setTilingQuickLayoutSlot(n, id) {
            settingsController.setTilingQuickLayoutSlot(n, id);
            tilingQuickLayoutSlotsChanged();
        }

        // App rules
        function getAppRulesForLayout(id) {
            return settingsController.getAppRulesForLayout(id);
        }

        function setAppRulesForLayout(id, rules) {
            settingsController.setAppRulesForLayout(id, rules);
            appRulesRefreshed();
        }

        function addAppRuleToLayout(id, pattern, zone, screen) {
            settingsController.addAppRuleToLayout(id, pattern, zone, screen);
            appRulesRefreshed();
        }

        function removeAppRuleFromLayout(id, idx) {
            settingsController.removeAppRuleFromLayout(id, idx);
            appRulesRefreshed();
        }

        // Running windows
        function getRunningWindows() {
            return settingsController.getRunningWindows();
        }

        // Per-desktop assignments
        function hasExplicitAssignmentForScreenDesktop(screen, desktop) {
            return settingsController.hasExplicitAssignmentForScreenDesktop(screen, desktop);
        }

        function hasExplicitTilingAssignmentForScreenDesktop(screen, desktop) {
            return settingsController.hasExplicitTilingAssignmentForScreenDesktop(screen, desktop);
        }

        function hasExplicitAssignmentForScreenActivity(screen, activity) {
            return settingsController.hasExplicitAssignmentForScreenActivity(screen, activity);
        }

        function hasExplicitTilingAssignmentForScreenActivity(screen, activity) {
            return settingsController.hasExplicitTilingAssignmentForScreenActivity(screen, activity);
        }

        function getLayoutForScreenDesktop(screen, desktop) {
            return settingsController.getLayoutForScreenDesktop(screen, desktop);
        }

        function getSnappingLayoutForScreenDesktop(screen, desktop) {
            return settingsController.getSnappingLayoutForScreenDesktop(screen, desktop);
        }

        function getTilingLayoutForScreenDesktop(screen, desktop) {
            return settingsController.getTilingLayoutForScreenDesktop(screen, desktop);
        }

        function clearScreenDesktopAssignment(screen, desktop) {
            settingsController.clearScreenDesktopAssignment(screen, desktop);
            screenAssignmentsChanged();
        }

        function assignLayoutToScreenDesktop(screen, desktop, layout) {
            settingsController.assignLayoutToScreenDesktop(screen, desktop, layout);
            screenAssignmentsChanged();
        }

        function assignTilingLayoutToScreenDesktop(screen, desktop, layout) {
            settingsController.assignTilingLayoutToScreenDesktop(screen, desktop, layout);
            tilingScreenAssignmentsChanged();
        }

        function clearTilingScreenDesktopAssignment(screen, desktop) {
            settingsController.clearTilingScreenDesktopAssignment(screen, desktop);
            tilingScreenAssignmentsChanged();
        }

        // Per-activity assignments
        function getLayoutForScreenActivity(screen, activity) {
            return settingsController.getLayoutForScreenActivity(screen, activity);
        }

        function getSnappingLayoutForScreenActivity(screen, activity) {
            return settingsController.getSnappingLayoutForScreenActivity(screen, activity);
        }

        function getTilingLayoutForScreenActivity(screen, activity) {
            return settingsController.getTilingLayoutForScreenActivity(screen, activity);
        }

        function clearScreenActivityAssignment(screen, activity) {
            settingsController.clearScreenActivityAssignment(screen, activity);
            activityAssignmentsChanged();
        }

        function assignLayoutToScreenActivity(screen, activity, layout) {
            settingsController.assignLayoutToScreenActivity(screen, activity, layout);
            activityAssignmentsChanged();
        }

        function assignTilingLayoutToScreenActivity(screen, activity, layout) {
            settingsController.assignTilingLayoutToScreenActivity(screen, activity, layout);
            tilingActivityAssignmentsChanged();
        }

        function clearTilingScreenActivityAssignment(screen, activity) {
            settingsController.clearTilingScreenActivityAssignment(screen, activity);
            tilingActivityAssignmentsChanged();
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
