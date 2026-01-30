// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Quick layout slots card - Assign layouts to keyboard shortcuts
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
                width: ListView.view.width
                height: shortcutRowLayout.implicitHeight + Kirigami.Units.smallSpacing * 2
                required property int index

                RowLayout {
                    id: shortcutRowLayout
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        property string shortcut: root.kcm.getQuickLayoutShortcut(index + 1)
                        text: shortcut !== "" ? shortcut : i18n("Not assigned")
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 14 + Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                        font.family: "monospace"
                        opacity: shortcut !== "" ? 1.0 : 0.6
                    }

                    Item { Layout.fillWidth: true }

                    LayoutComboBox {
                        id: slotLayoutCombo
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                        kcm: root.kcm
                        noneText: i18n("None")
                        showPreview: true

                        property int slotNumber: index + 1

                        function updateFromSlot() {
                            currentLayoutId = root.kcm.getQuickLayoutSlot(slotNumber) || ""
                        }

                        Component.onCompleted: updateFromSlot()

                        Connections {
                            target: root.kcm
                            function onScreenAssignmentsChanged() {
                                slotLayoutCombo.updateFromSlot()
                            }
                        }

                        onActivated: {
                            let selectedValue = model[currentIndex].value
                            root.kcm.setQuickLayoutSlot(slotNumber, selectedValue)
                        }
                    }

                    ToolButton {
                        icon.name: "edit-clear"
                        onClicked: {
                            root.kcm.setQuickLayoutSlot(index + 1, "")
                            slotLayoutCombo.clearSelection()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Clear shortcut")
                        Accessible.name: i18n("Clear shortcut %1", index + 1)
                    }
                }
            }
        }
    }
}
