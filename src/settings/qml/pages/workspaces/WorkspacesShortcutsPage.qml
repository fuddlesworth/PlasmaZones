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
 * The quick-layout model applied to workspaces: nine fixed slots, each with
 * a factory chord (Meta+Shift+N, rebindable in KDE's Shortcuts settings),
 * and the page assigns WHICH named workspace the slot sends the active
 * window to. The chord itself is not editable here, exactly like the
 * snapping/tiling quick shortcuts. Names come from the Named Workspaces
 * leaf; a slot with no workspace assigned does nothing.
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
                    text: i18n("Each slot sends the active window to the workspace you assign here. The keys themselves can be changed in the system Shortcuts settings under PlasmaZones.")
                }

                Repeater {
                    // Nine slots, matching the quick-layout slot count the
                    // indexed key builders and the daemon's 1..9 entries use.
                    model: 9

                    delegate: ColumnLayout {
                        id: slotDelegate

                        required property int index
                        property int slotNumber: index + 1
                        property string shortcutText: {
                            void root._slotTick;
                            return appSettings.workspaceMoveSlotShortcut(slotDelegate.index);
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
                                    text: slotDelegate.shortcutText !== "" ? i18nc("%1 is a keyboard shortcut such as Meta+Shift+1", "Shortcut %1, moves the active window", slotDelegate.shortcutText) : i18n("No shortcut assigned")
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font: Kirigami.Theme.smallFont
                                    opacity: slotDelegate.shortcutText !== "" ? slotDelegate._captionOpacity : slotDelegate._emptyCaptionOpacity
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
