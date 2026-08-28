// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dynamic workspaces — Behavior leaf.
 *
 * Card order: the KWin consent warning first (only while the feature is on
 * without per-output desktops and without the recorded consent — the one
 * state where the daemon sits dormant), then the cap warning, then the
 * Behavior card. The feature's master enable is the
 * sidebar toggle on the Workspaces parent row (the placement-mode pattern),
 * so this page deliberately carries no duplicate enable switch; like the mode
 * pages, its rows stay editable while the feature is off.
 *
 * The consent warning grants only the latch (workspacesManageKWinPerOutput);
 * the daemon owns the actual kwinrc write and the reconfigure, and re-enters
 * its gate when that setting reaches it. Like every other setting on this
 * page, it reaches the daemon on Save, not on the click. No restart is
 * involved either way.
 *
 * The warning hides the moment the latch is set, which is BEFORE Save has
 * committed it. That is deliberate: a warning that stayed up would read as if
 * the consent had not registered. The daemon acts on Save.
 */
SettingsFlickable {
    id: root

    // Seeded once, then re-read by hand. kwinPerOutputDesktopsEnabled() is a
    // plain invokable (kwinrc has no change notification), so this is a
    // snapshot and NOT a binding, however much the initializer looks like one:
    // nothing it reads would ever make it re-evaluate. The Connections below
    // re-read it whenever a state that can change it from OUR side flips. An
    // external kwinrc edit is picked up the next time this page is built.
    // The seed lives in the declaration rather than in Component.onCompleted
    // because a `false` start paints one frame of the wrong consent banner for
    // anyone who already has the KWin setting on.
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
            text: i18n("Dynamic workspaces need KWin to switch virtual desktops independently on each monitor. PlasmaZones can turn on KWin's per-output virtual desktops setting when you save. It never turns the setting back off.")
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
