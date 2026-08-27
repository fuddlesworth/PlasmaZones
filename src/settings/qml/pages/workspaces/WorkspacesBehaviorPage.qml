// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dynamic workspaces — Behavior leaf.
 *
 * Card order: the permanent explainer first (what the feature does), then the
 * KWin consent warning (only while the feature is on without per-output
 * desktops and without the recorded consent — the one state where the daemon
 * sits dormant), then the Behavior card. The feature's master enable is the
 * sidebar toggle on the Workspaces parent row (the placement-mode pattern),
 * so this page deliberately carries no duplicate enable switch; like the mode
 * pages, its rows stay editable while the feature is off.
 *
 * The consent warning grants only the latch (workspacesManageKWinPerOutput);
 * the daemon owns the actual kwinrc write + reconfigure and re-enters its
 * gate on the settings change, so accepting here takes effect immediately
 * with no restart.
 */
SettingsFlickable {
    id: root

    // kwinPerOutputDesktopsEnabled() is a plain invokable (kwinrc has no
    // change notification), so re-read it whenever the states that can
    // change it from OUR side flip; an external kwinrc edit is picked up on
    // the next page visit.
    property bool _kwinPerOutput: settingsController.kwinPerOutputDesktopsEnabled()

    Connections {
        function onWorkspacesEnabledChanged() {
            root._kwinPerOutput = settingsController.kwinPerOutputDesktopsEnabled();
        }

        function onWorkspacesManageKWinPerOutputChanged() {
            root._kwinPerOutput = settingsController.kwinPerOutputDesktopsEnabled();
        }

        target: appSettings
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // No page-level info banner — the mode pages carry their explanation
        // in row descriptions, and this page does the same. The two
        // InlineMessages below are true WARNINGS, hidden in the normal state.
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            text: i18n("Dynamic workspaces need KWin to switch virtual desktops independently on each monitor. PlasmaZones can turn on KWin's per-output virtual desktops setting. The change takes effect immediately, and PlasmaZones does not turn it back off.")
            visible: appSettings.workspacesEnabled && !root._kwinPerOutput && !appSettings.workspacesManageKWinPerOutput

            actions: [
                Kirigami.Action {
                    text: i18n("Turn On KWin Setting")
                    icon.name: "dialog-ok-apply"
                    onTriggered: appSettings.workspacesManageKWinPerOutput = true
                }
            ]
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            text: i18n("Workspace limit reached. New workspaces cannot be added until one is removed.")
            visible: appSettings.workspacesEnabled && settingsController.workspacesAtCap
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Behavior")
            searchAnchor: "workspacesBehavior"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Dynamic workspace lists")
                    searchAnchor: "workspacesDynamicLists"
                    description: i18n("Each monitor keeps its own list of workspaces. Opening a window on the last empty workspace adds a new one, and an emptied workspace disappears.")
                    // Informational row: the behavior itself IS the feature
                    // (toggled from the sidebar), so there is no control here.
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Workspace hint")
                    searchAnchor: "workspacesSnapBackOsdHint"
                    description: i18n("Show a hint when a switch lands on a workspace that belongs to another monitor and is sent back.")

                    SettingsSwitch {
                        accessibleName: i18n("Show workspace hint")
                        checked: appSettings.workspacesSnapBackOsdHint
                        onToggled: function (newValue) {
                            appSettings.workspacesSnapBackOsdHint = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Replace KWin desktop shortcuts")
                    searchAnchor: "workspacesRebindKWinShortcuts"
                    description: i18n("While dynamic workspaces are on, take over the KWin desktop switching shortcuts so they move through this monitor's workspaces. They are restored when the feature is turned off.")

                    SettingsSwitch {
                        accessibleName: i18n("Replace KWin desktop shortcuts")
                        checked: appSettings.workspacesRebindKWinShortcuts
                        onToggled: function (newValue) {
                            appSettings.workspacesRebindKWinShortcuts = newValue;
                        }
                    }
                }
            }
        }
    }
}
