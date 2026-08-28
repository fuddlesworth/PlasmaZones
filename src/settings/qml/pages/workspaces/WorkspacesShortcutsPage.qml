// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dynamic workspaces — Quick Shortcuts leaf.
 *
 * The quick-layout model applied to workspaces: a fixed set of slots, each
 * with a factory chord, and the page assigns WHICH named workspace the slot
 * acts on. The chords themselves are not editable here, exactly like the
 * snapping/tiling quick shortcuts. Names come from the Named Workspaces leaf;
 * a slot with no workspace assigned does nothing.
 *
 * Each slot carries TWO daemon-bound chords: the move chord sends the active
 * window to the assigned workspace, and the focus chord jumps to the slot's
 * position in the acting monitor's list. Both are shown, because a chord the
 * daemon binds but the page never mentions is a chord the user cannot
 * discover.
 */
SettingsFlickable {
    id: root

    // Declared names for the combos (the same shim RulesPage uses).
    readonly property var workspaceNames: {
        var names = [];
        var entries = appSettings.workspacesNamedEntries;
        for (var i = 0; i < entries.length; ++i) {
            var name = ("" + (entries[i].name || "")).trim();
            if (name.length > 0)
                names.push(name);
        }
        return names;
    }
    // Bumped on target/shortcut changes so the invokable reads re-evaluate.
    property int _slotTick: 0

    Connections {
        function onWorkspaceSlotTargetsChanged() {
            root._slotTick++;
        }

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
                    // Names BOTH verbs and where the keys live. The page only
                    // assigns which workspace a slot acts on, so a user who
                    // wants a different chord has nowhere to go from here
                    // unless the text says so. Worded without naming the KDE
                    // System Settings module, which the portable
                    // (USE_KDE_FRAMEWORKS=OFF) build does not have.
                    text: i18n("Each slot carries two keys. One jumps to the workspace in that position and the other sends the active window there. Both are registered under PlasmaZones and can be changed in your desktop's keyboard shortcut settings.")
                }

                Repeater {
                    // The quick-slot count the indexed key builders and the
                    // daemon's slot entries share
                    // (ConfigDefaults::WorkspaceSlotCount).
                    model: settingsController.workspaceSlotCount

                    delegate: ColumnLayout {
                        id: slotDelegate

                        required property int index
                        property int slotNumber: index + 1
                        property string shortcutText: {
                            void root._slotTick;
                            return appSettings.workspaceMoveSlotShortcut(slotDelegate.index);
                        }
                        property string focusShortcutText: {
                            void root._slotTick;
                            return appSettings.workspaceFocusSlotShortcut(slotDelegate.index);
                        }
                        property string targetName: {
                            void root._slotTick;
                            return appSettings.workspaceSlotTarget(slotDelegate.index);
                        }
                        readonly property real _captionOpacity: 0.6
                        readonly property real _emptyCaptionOpacity: 0.35

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

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                                spacing: Kirigami.Units.smallSpacing / 2

                                Label {
                                    text: i18n("Quick Workspace %1", slotDelegate.slotNumber)
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: slotDelegate.shortcutText !== "" ? i18nc("%1 is a keyboard shortcut such as Meta+Shift+1", "%1 moves the active window here", slotDelegate.shortcutText) : i18n("No move shortcut assigned")
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font: Kirigami.Theme.smallFont
                                    opacity: slotDelegate.shortcutText !== "" ? slotDelegate._captionOpacity : slotDelegate._emptyCaptionOpacity
                                }

                                // The slot's OTHER daemon-bound chord. It is
                                // positional, not name-based: it focuses this
                                // slot's place in the monitor's workspace list
                                // (WorkspaceController::focusWorkspaceAt), so
                                // it does not read the assignment beside it.
                                // Hidden when unbound rather than showing a
                                // second "none" line under the first.
                                Label {
                                    visible: slotDelegate.focusShortcutText !== ""
                                    text: i18nc("%1 is a keyboard shortcut such as Meta+1", "%1 switches to the workspace in this position", slotDelegate.focusShortcutText)
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font: Kirigami.Theme.smallFont
                                    opacity: slotDelegate._captionOpacity
                                }
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                                spacing: Kirigami.Units.smallSpacing

                                WideComboBox {
                                    id: slotCombo

                                    Layout.fillWidth: true
                                    Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                                    model: {
                                        var items = [
                                            {
                                                "label": i18n("None"),
                                                "value": ""
                                            }
                                        ];
                                        for (var i = 0; i < root.workspaceNames.length; ++i)
                                            items.push({
                                                "label": root.workspaceNames[i],
                                                "value": root.workspaceNames[i]
                                            });
                                        return items;
                                    }
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: {
                                        for (var i = 0; i < model.length; ++i) {
                                            if (model[i].value === slotDelegate.targetName)
                                                return i;
                                        }
                                        return -1;
                                    }
                                    // An assigned name whose declaration was
                                    // removed stays legible: the slot is
                                    // dormant, not broken.
                                    displayText: currentIndex >= 0 ? currentText : slotDelegate.targetName
                                    Accessible.name: i18n("Workspace for quick shortcut %1", slotDelegate.slotNumber)
                                    onActivated: function (index) {
                                        var entry = model[index];
                                        var value = entry ? (entry.value || "") : "";
                                        if (value !== slotDelegate.targetName)
                                            appSettings.setWorkspaceSlotTarget(slotDelegate.index, value);
                                    }
                                }

                                ToolButton {
                                    icon.name: "edit-clear"
                                    enabled: slotDelegate.targetName !== ""
                                    onClicked: appSettings.setWorkspaceSlotTarget(slotDelegate.index, "")
                                    ToolTip.visible: hovered
                                    ToolTip.text: i18n("Clear workspace")
                                    Accessible.name: i18n("Clear workspace for quick shortcut %1", slotDelegate.slotNumber)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
