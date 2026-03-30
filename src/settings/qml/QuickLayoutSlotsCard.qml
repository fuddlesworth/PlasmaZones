// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Quick layout slots card - Assign layouts to keyboard shortcuts
 */
SettingsCard {
    id: root

    required property var appSettings
    // 0 = snapping (zone layouts), 1 = tiling (autotile algorithms)
    property int viewMode: 0

    function getSlot(slotNumber) {
        return viewMode === 1 ? (appSettings.getTilingQuickLayoutSlot(slotNumber) || "") : (appSettings.getQuickLayoutSlot(slotNumber) || "");
    }

    function setSlot(slotNumber, value) {
        if (viewMode === 1)
            appSettings.setTilingQuickLayoutSlot(slotNumber, value);
        else
            appSettings.setQuickLayoutSlot(slotNumber, value);
    }

    headerText: root.viewMode === 1 ? i18n("Tiling Quick Shortcuts") : i18n("Quick Layout Shortcuts")
    collapsible: true

    contentItem: ColumnLayout {
        spacing: 0

        Repeater {
            model: 9

            delegate: ColumnLayout {
                id: slotDelegate

                required property int index
                property int slotNumber: index + 1
                property string shortcutText: root.appSettings.getQuickLayoutShortcut(slotNumber)
                property int _slotRevision: 0

                Layout.fillWidth: true
                spacing: 0

                // Separator between items (not before the first)
                SettingsSeparator {
                    visible: slotDelegate.index > 0
                }

                // Slot row — matches SettingsRow layout: title+description left, control right
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.largeSpacing

                    // Left: slot title + shortcut info
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                        spacing: Kirigami.Units.smallSpacing / 2

                        Label {
                            text: root.viewMode === 1 ? i18n("Quick Tiling %1", slotDelegate.slotNumber) : i18n("Quick Layout %1", slotDelegate.slotNumber)
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Label {
                            text: slotDelegate.shortcutText !== "" ? slotDelegate.shortcutText : i18n("No shortcut assigned")
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font: Kirigami.Theme.smallFont
                            opacity: slotDelegate.shortcutText !== "" ? 0.6 : 0.35
                        }

                    }

                    // Right: layout combo + clear button
                    RowLayout {
                        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                        Layout.maximumWidth: parent.width * 0.45
                        spacing: Kirigami.Units.smallSpacing

                        LayoutComboBox {
                            id: slotLayoutCombo

                            Layout.fillWidth: true
                            Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                            appSettings: root.appSettings
                            noneText: i18n("None")
                            showPreview: root.viewMode === 0
                            layoutFilter: root.viewMode === 1 ? 1 : 0
                            resolvedDefaultId: ""
                            currentLayoutId: {
                                void (slotDelegate._slotRevision);
                                return root.getSlot(slotDelegate.slotNumber);
                            }
                            onActivated: {
                                root.setSlot(slotDelegate.slotNumber, model[currentIndex].value);
                            }

                            Connections {
                                function onQuickLayoutSlotsChanged() {
                                    slotDelegate._slotRevision++;
                                }

                                function onTilingQuickLayoutSlotsChanged() {
                                    slotDelegate._slotRevision++;
                                }

                                target: root.appSettings
                            }

                        }

                        ToolButton {
                            icon.name: "edit-clear"
                            onClicked: {
                                root.setSlot(slotDelegate.slotNumber, "");
                                slotLayoutCombo.clearSelection();
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Clear shortcut")
                            Accessible.name: i18n("Clear shortcut %1", slotDelegate.slotNumber)
                        }

                    }

                }

            }

        }

    }

}
