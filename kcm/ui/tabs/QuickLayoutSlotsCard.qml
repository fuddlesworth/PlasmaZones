// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Quick layout slots card - Assign layouts to keyboard shortcuts
 *
 * Refactored to use cleaner structure with LayoutComboBox.
 */
Kirigami.Card {
    id: root

    required property var kcm
    required property QtObject constants

    header: Kirigami.Heading {
        level: 3
        text: i18n("Quick Layout Shortcuts")
        padding: Kirigami.Units.smallSpacing
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            text: i18n("Assign layouts to keyboard shortcuts for instant switching.")
            wrapMode: Text.WordWrap
            opacity: root.constants.labelSecondaryOpacity
        }

        ListView {
            id: quickLayoutListView
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            Layout.margins: Kirigami.Units.smallSpacing
            clip: true
            model: root.constants.quickLayoutSlotCount
            interactive: false

            Accessible.name: i18n("Quick layout shortcuts list")
            Accessible.role: Accessible.List

            delegate: Item {
                id: slotDelegate
                width: ListView.view.width
                height: slotRow.implicitHeight + Kirigami.Units.smallSpacing * 2
                required property int index

                property int slotNumber: index + 1
                property string shortcutText: root.kcm.getQuickLayoutShortcut(slotNumber)

                RowLayout {
                    id: slotRow
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    // Shortcut display
                    Label {
                        text: slotDelegate.shortcutText !== "" ? slotDelegate.shortcutText : i18n("Not assigned")
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                        font.family: "monospace"
                        opacity: slotDelegate.shortcutText !== "" ? 1.0 : 0.6
                    }

                    Item { Layout.fillWidth: true }

                    // Layout selection
                    LayoutComboBox {
                        id: slotLayoutCombo
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                        kcm: root.kcm
                        noneText: i18n("None")
                        showPreview: true
                        resolvedDefaultId: ""  // "None" means no assignment, not a resolution to something
                        currentLayoutId: root.kcm.getQuickLayoutSlot(slotDelegate.slotNumber) || ""

                        Connections {
                            target: root.kcm
                            function onScreenAssignmentsChanged() {
                                slotLayoutCombo.currentLayoutId = root.kcm.getQuickLayoutSlot(slotDelegate.slotNumber) || ""
                            }
                        }

                        onActivated: {
                            root.kcm.setQuickLayoutSlot(slotDelegate.slotNumber, model[currentIndex].value)
                        }
                    }

                    ToolButton {
                        icon.name: "edit-clear"
                        onClicked: {
                            root.kcm.setQuickLayoutSlot(slotDelegate.slotNumber, "")
                            slotLayoutCombo.clearSelection()
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
