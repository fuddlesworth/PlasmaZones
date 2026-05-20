// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.settings

SettingsFlickable {
    id: root

    readonly property var settingsBridge: SnappingBridge {}

    // AssignmentEntry.Snapping names the same integer as the C++ enum so a
    // future renumber stays in sync; magic 0 here would silently desync.
    readonly property int viewMode: AssignmentEntry.Snapping

    contentHeight: mainCol.implicitHeight
    clip: true

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Assign zone layouts to monitors, virtual desktops, and activities.")
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
