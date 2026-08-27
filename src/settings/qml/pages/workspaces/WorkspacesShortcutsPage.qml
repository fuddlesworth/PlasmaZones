// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dynamic workspaces — Quick Shortcuts leaf.
 *
 * The per-mode Quick Shortcuts pattern applied to workspaces: nine fixed
 * slots, each "move the active window to workspace N of the current
 * monitor", with the slot's chord as the only editable control (unset by
 * default — bind the slots you use). The general workspace verbs (focus
 * up/down, move up/down, reorder, move to monitor) are ordinary daemon
 * global shortcuts, edited in KDE's Shortcuts settings like every other
 * PlasmaZones chord — they deliberately have no duplicate editors here.
 * The per-NAMED-workspace chords live on each entry in the Named Workspaces
 * leaf, beside the name they bind.
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
            headerText: i18n("Workspace Quick Shortcuts")
            searchAnchor: "workspaceQuickShortcuts"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    text: i18n("Send the active window to a specific workspace on its monitor with one key. The general workspace shortcuts are in the system Shortcuts settings under PlasmaZones.")
                }

                Repeater {
                    // Nine slots, matching the quick-layout slot count the
                    // indexed key builders and the daemon's 1..9 entries use.
                    model: 9

                    delegate: ColumnLayout {
                        id: slotDelegate

                        required property int index
                        property int slotNumber: index + 1

                        Layout.fillWidth: true
                        spacing: 0

                        SettingsSeparator {
                            visible: slotDelegate.index > 0
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.smallSpacing
                            Layout.leftMargin: Kirigami.Units.largeSpacing
                            Layout.rightMargin: Kirigami.Units.largeSpacing
                            spacing: Kirigami.Units.largeSpacing

                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: i18n("Move to workspace %1", slotDelegate.slotNumber)
                            }

                            ShortcutCaptureField {
                                accessibleName: i18n("Move window to workspace %1", slotDelegate.slotNumber)
                                keySequence: {
                                    void root._slotTick; // deliberate dependency registration
                                    return appSettings.workspaceMoveSlotShortcut(slotDelegate.index);
                                }
                                onKeySequenceModified: seq => {
                                    appSettings.setWorkspaceMoveSlotShortcut(slotDelegate.index, seq);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
