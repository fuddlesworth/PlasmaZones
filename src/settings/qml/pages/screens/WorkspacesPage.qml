// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dynamic per-monitor workspaces settings.
 *
 * The enable toggle carries the KWin per-output consent flow: turning the
 * feature on while KWin's PerOutputVirtualDesktops is off surfaces an inline
 * confirmation; accepting records the consent latch and the daemon writes the
 * kwinrc key and reconfigures KWin (never silently). The named-workspace list
 * edits the Workspaces.Named/Entries declarations in place; the daemon
 * reacts live (create/rename/unpin) without a restart.
 */
SettingsFlickable {
    id: root

    // Staged copy of appSettings.workspacesNamedEntries (array of maps with
    // name/output/position/focusShortcut/moveShortcut). Committed whole on
    // every edit — the declarations are a whole-replace composite.
    property var _entries: []
    property bool _consentPending: false

    function _loadEntries() {
        var stored = appSettings.workspacesNamedEntries;
        var copy = [];
        for (var i = 0; i < stored.length; ++i)
            copy.push({
                "name": stored[i].name || "",
                "output": stored[i].output || "",
                "position": stored[i].position !== undefined ? stored[i].position : -1,
                "focusShortcut": stored[i].focusShortcut || "",
                "moveShortcut": stored[i].moveShortcut || ""
            });
        _entries = copy;
    }

    function _commitEntries() {
        appSettings.workspacesNamedEntries = _entries;
    }

    Component.onCompleted: _loadEntries()

    Connections {
        function onWorkspacesNamedEntriesChanged() {
            root._loadEntries();
        }

        target: appSettings
    }

    contentHeight: mainCol.implicitHeight
    clip: true

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Each monitor keeps its own list of workspaces. Opening a window on the last empty workspace adds a new one, and an emptied workspace disappears. Named workspaces stay even when empty.")
            visible: true
        }

        // ── Enable + consent ────────────────────────────────────────────
        Switch {
            id: enableSwitch

            Layout.fillWidth: true
            text: i18n("Enable dynamic workspaces")
            checked: appSettings.workspacesEnabled
            Accessible.name: text
            onToggled: {
                if (checked && !settingsController.kwinPerOutputDesktopsEnabled() && !appSettings.workspacesManageKWinPerOutput) {
                    // Ask before touching kwinrc; the switch stays visually on
                    // while the confirmation is pending, and declining reverts.
                    root._consentPending = true;
                } else {
                    appSettings.workspacesEnabled = checked;
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: root._consentPending
            text: i18n("Dynamic workspaces need KWin to switch virtual desktops independently on each monitor. PlasmaZones will turn on KWin's per-output virtual desktops setting. The change takes effect immediately, and PlasmaZones does not turn it back off.")

            actions: [
                Kirigami.Action {
                    text: i18n("Turn On and Enable")
                    icon.name: "dialog-ok-apply"
                    onTriggered: {
                        appSettings.workspacesManageKWinPerOutput = true;
                        appSettings.workspacesEnabled = true;
                        root._consentPending = false;
                    }
                },
                Kirigami.Action {
                    text: i18n("Cancel")
                    icon.name: "dialog-cancel"
                    onTriggered: {
                        enableSwitch.checked = false;
                        root._consentPending = false;
                    }
                }
            ]
        }

        Switch {
            Layout.fillWidth: true
            text: i18n("Show a hint when a workspace belongs to another monitor")
            checked: appSettings.workspacesSnapBackOsdHint
            enabled: appSettings.workspacesEnabled
            Accessible.name: text
            onToggled: appSettings.workspacesSnapBackOsdHint = checked
        }

        Switch {
            Layout.fillWidth: true
            text: i18n("Replace the KWin desktop switching shortcuts while enabled")
            checked: appSettings.workspacesRebindKWinShortcuts
            enabled: appSettings.workspacesEnabled
            Accessible.name: text
            onToggled: appSettings.workspacesRebindKWinShortcuts = checked
        }

        // ── Named workspaces ────────────────────────────────────────────
        Kirigami.Heading {
            Layout.topMargin: Kirigami.Units.largeSpacing
            level: 3
            text: i18n("Named Workspaces")
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.8
            text: i18n("A named workspace is created at login, keeps its place while empty, and can be pinned to a monitor. Shortcuts jump to it or send the active window there.")
        }

        Repeater {
            model: root._entries.length

            delegate: Kirigami.AbstractCard {
                id: entryCard

                required property int index

                Layout.fillWidth: true

                contentItem: GridLayout {
                    columns: 2
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Name")
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        TextField {
                            id: nameField

                            Layout.fillWidth: true
                            text: root._entries[entryCard.index].name
                            placeholderText: i18n("chat")
                            Accessible.name: i18n("Workspace name")
                            onEditingFinished: {
                                var trimmed = text.trim();
                                for (var i = 0; i < root._entries.length; ++i)
                                    if (i !== entryCard.index && root._entries[i].name === trimmed)
                                        return; // names are unique; refuse the duplicate

                                root._entries[entryCard.index].name = trimmed;
                                root._commitEntries();
                            }
                        }

                        ToolButton {
                            icon.name: "arrow-up"
                            enabled: entryCard.index > 0
                            Accessible.name: i18n("Move up")
                            onClicked: {
                                var arr = root._entries;
                                var tmp = arr[entryCard.index - 1];
                                arr[entryCard.index - 1] = arr[entryCard.index];
                                arr[entryCard.index] = tmp;
                                root._entries = arr;
                                root._commitEntries();
                            }
                        }

                        ToolButton {
                            icon.name: "arrow-down"
                            enabled: entryCard.index < root._entries.length - 1
                            Accessible.name: i18n("Move down")
                            onClicked: {
                                var arr = root._entries;
                                var tmp = arr[entryCard.index + 1];
                                arr[entryCard.index + 1] = arr[entryCard.index];
                                arr[entryCard.index] = tmp;
                                root._entries = arr;
                                root._commitEntries();
                            }
                        }

                        ToolButton {
                            icon.name: "edit-delete"
                            Accessible.name: i18n("Remove named workspace")
                            onClicked: {
                                var arr = root._entries;
                                arr.splice(entryCard.index, 1);
                                root._entries = arr;
                                root._commitEntries();
                            }
                        }
                    }

                    Label {
                        text: i18n("Monitor")
                    }

                    ComboBox {
                        id: outputCombo

                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "value"
                        Accessible.name: i18n("Pinned monitor")
                        model: {
                            var options = [
                                {
                                    "label": i18n("Any monitor"),
                                    "value": ""
                                }
                            ];
                            var screens = settingsController.screens || [];
                            for (var i = 0; i < screens.length; ++i)
                                options.push({
                                    "label": screens[i].displayLabel || screens[i].name,
                                    "value": screens[i].name
                                });
                            return options;
                        }
                        Component.onCompleted: currentIndex = indexOfValue(root._entries[entryCard.index].output)
                        onActivated: {
                            root._entries[entryCard.index].output = currentValue;
                            root._commitEntries();
                        }
                    }

                    Label {
                        text: i18n("Focus shortcut")
                    }

                    ShortcutCaptureField {
                        Layout.fillWidth: true
                        accessibleName: i18n("Focus named workspace shortcut")
                        keySequence: root._entries[entryCard.index].focusShortcut
                        onKeySequenceModified: seq => {
                            root._entries[entryCard.index].focusShortcut = seq;
                            root._commitEntries();
                        }
                    }

                    Label {
                        text: i18n("Move window shortcut")
                    }

                    ShortcutCaptureField {
                        Layout.fillWidth: true
                        accessibleName: i18n("Move window to named workspace shortcut")
                        keySequence: root._entries[entryCard.index].moveShortcut
                        onKeySequenceModified: seq => {
                            root._entries[entryCard.index].moveShortcut = seq;
                            root._commitEntries();
                        }
                    }
                }
            }
        }

        Button {
            text: i18n("Add Named Workspace")
            icon.name: "list-add"
            enabled: appSettings.workspacesEnabled
            Accessible.name: text
            onClicked: {
                var arr = root._entries;
                arr.push({
                    "name": "",
                    "output": "",
                    "position": -1,
                    "focusShortcut": "",
                    "moveShortcut": ""
                });
                root._entries = arr;
                // Not committed until the name is filled in — an empty name
                // is skipped by the daemon anyway.
            }
        }
    }
}
