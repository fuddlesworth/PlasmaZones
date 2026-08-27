// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dynamic workspaces — Shortcuts leaf.
 *
 * Card order: navigation first (the verbs a user reaches for immediately),
 * then window/column movement, then workspace rearrangement, then the indexed
 * slot pairs (unset by default — the quick-layout-slot convention, bind only
 * what you use). Every field writes its Settings chord; the daemon's
 * ShortcutManager rebinds live on the settings save, so edits apply without a
 * restart. The per-NAMED-workspace chords are not here — they live on each
 * entry in the Named Workspaces leaf, beside the name they bind.
 *
 * The indexed slots have no per-slot Q_PROPERTY (nine properties per family
 * would be pure boilerplate); the fields read through the Q_INVOKABLE indexed
 * getters, re-evaluated via _slotTick on the single shared change signal.
 */
SettingsFlickable {
    id: root

    // Bumped on workspaceSlotShortcutsChanged so the invokable reads below
    // re-evaluate (invokables register no property dependency of their own).
    property int _slotTick: 0

    Connections {
        function onWorkspaceSlotShortcutsChanged() {
            root._slotTick++;
        }

        target: appSettings
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Navigation")
            searchAnchor: "workspacesShortcutsNavigation"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Focus workspace above")
                    searchAnchor: "workspaceFocusUpShortcut"

                    ShortcutCaptureField {
                        accessibleName: i18n("Focus workspace above")
                        keySequence: appSettings.workspaceFocusUpShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceFocusUpShortcut = seq;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Focus workspace below")
                    searchAnchor: "workspaceFocusDownShortcut"

                    ShortcutCaptureField {
                        accessibleName: i18n("Focus workspace below")
                        keySequence: appSettings.workspaceFocusDownShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceFocusDownShortcut = seq;
                        }
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Move windows and columns")
            searchAnchor: "workspacesShortcutsMove"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Move window to workspace above")
                    searchAnchor: "workspaceMoveWindowUpShortcut"

                    ShortcutCaptureField {
                        accessibleName: i18n("Move window to workspace above")
                        keySequence: appSettings.workspaceMoveWindowUpShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceMoveWindowUpShortcut = seq;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Move window to workspace below")
                    searchAnchor: "workspaceMoveWindowDownShortcut"

                    ShortcutCaptureField {
                        accessibleName: i18n("Move window to workspace below")
                        keySequence: appSettings.workspaceMoveWindowDownShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceMoveWindowDownShortcut = seq;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Move column to workspace above")
                    searchAnchor: "workspaceMoveColumnUpShortcut"
                    description: i18n("Scrolling screens only. The focused column moves as a group.")

                    ShortcutCaptureField {
                        accessibleName: i18n("Move column to workspace above")
                        keySequence: appSettings.workspaceMoveColumnUpShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceMoveColumnUpShortcut = seq;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Move column to workspace below")
                    searchAnchor: "workspaceMoveColumnDownShortcut"
                    description: i18n("Scrolling screens only. The focused column moves as a group.")

                    ShortcutCaptureField {
                        accessibleName: i18n("Move column to workspace below")
                        keySequence: appSettings.workspaceMoveColumnDownShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceMoveColumnDownShortcut = seq;
                        }
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Rearrange workspaces")
            searchAnchor: "workspacesShortcutsRearrange"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Move workspace up")
                    searchAnchor: "workspaceReorderUpShortcut"

                    ShortcutCaptureField {
                        accessibleName: i18n("Move workspace up")
                        keySequence: appSettings.workspaceReorderUpShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceReorderUpShortcut = seq;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Move workspace down")
                    searchAnchor: "workspaceReorderDownShortcut"

                    ShortcutCaptureField {
                        accessibleName: i18n("Move workspace down")
                        keySequence: appSettings.workspaceReorderDownShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceReorderDownShortcut = seq;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Move workspace to the monitor on the left")
                    searchAnchor: "workspaceMoveToMonitorLeftShortcut"

                    ShortcutCaptureField {
                        accessibleName: i18n("Move workspace to the monitor on the left")
                        keySequence: appSettings.workspaceMoveToMonitorLeftShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceMoveToMonitorLeftShortcut = seq;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Move workspace to the monitor on the right")
                    searchAnchor: "workspaceMoveToMonitorRightShortcut"

                    ShortcutCaptureField {
                        accessibleName: i18n("Move workspace to the monitor on the right")
                        keySequence: appSettings.workspaceMoveToMonitorRightShortcut
                        onKeySequenceModified: seq => {
                            appSettings.workspaceMoveToMonitorRightShortcut = seq;
                        }
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Workspace slots")
            searchAnchor: "workspacesShortcutsSlots"
            collapsible: true
            initiallyCollapsed: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: 9

                    delegate: ColumnLayout {
                        id: slotRow

                        required property int index

                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        SettingsRow {
                            title: i18n("Focus workspace %1", slotRow.index + 1)
                            searchAnchor: "workspaceFocusSlot" + (slotRow.index + 1)

                            ShortcutCaptureField {
                                accessibleName: i18n("Focus workspace %1", slotRow.index + 1)
                                keySequence: {
                                    void root._slotTick; // deliberate dependency registration
                                    return appSettings.workspaceFocusSlotShortcut(slotRow.index);
                                }
                                onKeySequenceModified: seq => {
                                    appSettings.setWorkspaceFocusSlotShortcut(slotRow.index, seq);
                                }
                            }
                        }

                        SettingsRow {
                            title: i18n("Move window to workspace %1", slotRow.index + 1)
                            searchAnchor: "workspaceMoveSlot" + (slotRow.index + 1)

                            ShortcutCaptureField {
                                accessibleName: i18n("Move window to workspace %1", slotRow.index + 1)
                                keySequence: {
                                    void root._slotTick; // deliberate dependency registration
                                    return appSettings.workspaceMoveSlotShortcut(slotRow.index);
                                }
                                onKeySequenceModified: seq => {
                                    appSettings.setWorkspaceMoveSlotShortcut(slotRow.index, seq);
                                }
                            }
                        }

                        SettingsSeparator {
                            visible: slotRow.index < 8
                        }
                    }
                }
            }
        }
    }
}
