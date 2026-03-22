// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property var
    settingsBridge: QtObject {
        readonly property bool autotileEnabled: appSettings.autotileEnabled
        readonly property string autotileAlgorithm: appSettings.autotileAlgorithm
        readonly property string defaultLayoutId: appSettings.defaultLayoutId
        readonly property var screens: settingsController.screens
        readonly property var layouts: settingsController.layouts
        readonly property int assignmentViewMode: 1
        readonly property int virtualDesktopCount: settingsController.virtualDesktopCount
        readonly property var virtualDesktopNames: settingsController.virtualDesktopNames
        readonly property var disabledMonitors: appSettings.disabledMonitors
        readonly property bool activitiesAvailable: settingsController.activitiesAvailable
        readonly property var activities: settingsController.activities
        readonly property string currentActivity: settingsController.currentActivity

        signal screenAssignmentsChanged()
        signal tilingScreenAssignmentsChanged()
        signal tilingDesktopAssignmentsChanged()
        signal lockedScreensChanged()
        signal activityAssignmentsChanged()
        signal tilingActivityAssignmentsChanged()

        // Monitor disabled
        function isMonitorDisabled(name) {
            return settingsController.isMonitorDisabled(name);
        }

        function setMonitorDisabled(name, disabled) {
            settingsController.setMonitorDisabled(name, disabled);
        }

        // Tiling screen assignments
        function assignTilingLayoutToScreen(screen, layout) {
            settingsController.assignTilingLayoutToScreen(screen, layout);
            tilingScreenAssignmentsChanged();
        }

        function clearTilingScreenAssignment(screen) {
            settingsController.clearTilingScreenAssignment(screen);
            tilingScreenAssignmentsChanged();
        }

        function getTilingLayoutForScreen(screen) {
            return settingsController.getTilingLayoutForScreen(screen);
        }

        // Screen locking
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

        // Per-desktop tiling assignments
        function hasExplicitTilingAssignmentForScreenDesktop(screen, desktop) {
            return settingsController.hasExplicitTilingAssignmentForScreenDesktop(screen, desktop);
        }

        function getTilingLayoutForScreenDesktop(screen, desktop) {
            return settingsController.getTilingLayoutForScreenDesktop(screen, desktop);
        }

        function assignTilingLayoutToScreenDesktop(screen, desktop, layout) {
            settingsController.assignTilingLayoutToScreenDesktop(screen, desktop, layout);
            tilingScreenAssignmentsChanged();
        }

        function clearTilingScreenDesktopAssignment(screen, desktop) {
            settingsController.clearTilingScreenDesktopAssignment(screen, desktop);
            tilingScreenAssignmentsChanged();
        }

        // Per-activity tiling assignments
        function hasExplicitTilingAssignmentForScreenActivity(screen, activity) {
            return settingsController.hasExplicitTilingAssignmentForScreenActivity(screen, activity);
        }

        function getTilingLayoutForScreenActivity(screen, activity) {
            return settingsController.getTilingLayoutForScreenActivity(screen, activity);
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

    readonly property int viewMode: 1

    contentHeight: mainCol.implicitHeight
    clip: true

    QtObject {
        id: constants

        readonly property real labelSecondaryOpacity: 0.7
    }

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Assign autotile algorithms to monitors, virtual desktops, and activities.")
            visible: true
        }

        // Monitor Assignments
        Item {
            Layout.fillWidth: true
            implicitHeight: monitorCard.implicitHeight

            MonitorAssignmentsCard {
                id: monitorCard

                anchors.fill: parent
                appSettings: root.settingsBridge
                constants: constants
                viewMode: root.viewMode
            }

        }

        // Activity Assignments
        Item {
            Layout.fillWidth: true
            implicitHeight: activityCard.implicitHeight
            visible: root.settingsBridge.activitiesAvailable

            ActivityAssignmentsCard {
                id: activityCard

                anchors.fill: parent
                appSettings: root.settingsBridge
                constants: constants
                viewMode: root.viewMode
            }

        }

        // Info when Activities not available
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: !root.settingsBridge.activitiesAvailable && root.settingsBridge.screens.length > 0
            type: Kirigami.MessageType.Information
            text: i18n("KDE Activities support is not available. Activity-based layout assignments require the KDE Activities service to be running.")
        }

    }

}
