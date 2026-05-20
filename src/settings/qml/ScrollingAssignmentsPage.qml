// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Assignments sub-page.
 *
 * Gates scrolling mode per monitor / virtual desktop / activity. A screen is
 * put into scroll mode through the Layouts picker (the synthetic "Scrolling"
 * entry); this page is the per-context enable/disable surface — the
 * Scroll{Disabled}Monitors/Desktops/Activities lists — mirroring the disable
 * toggles on the Snapping and Tiling assignment pages, without the layout
 * dropdowns that scroll mode (a single mode, no per-context layout) has no
 * use for. Every toggle routes through ScrollingBridge → SettingsController
 * with the scroll view-mode.
 *
 * The per-monitor / per-desktop / per-activity list itself lives in
 * AssignmentMonitorList — shared with any future per-mode disable surface
 * so that all three modes pick up the Accessible.name fix from PR #493 and
 * the tick-based binding pattern in lockstep.
 */
SettingsFlickable {
    id: root

    readonly property var settingsBridge: ScrollingBridge {}

    contentHeight: mainCol.implicitHeight
    clip: true

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Choose where scrolling mode may run. Assign scrolling to a screen from the Layouts page; these toggles gate it per monitor, virtual desktop, and activity.")
            visible: true
        }

        AssignmentMonitorList {
            Layout.fillWidth: true
            settingsBridge: root.settingsBridge
            modeName: i18nc("@label scrolling mode name used inside accessible / tooltip strings", "Scrolling")
            activitiesDescription: i18n("Gate scrolling mode per KDE Activity, on each monitor.")
            activitiesUnavailableMessage: i18n("KDE Activities support is not available. Activity-based gating requires the KDE Activities service to be running.")
        }
    }
}
