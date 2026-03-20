// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Quick layout slots card - Assign layouts to keyboard shortcuts
 *
 * Refactored to use cleaner structure with LayoutComboBox.
 */
Kirigami.Card {
    id: root

    required property var kcm
    required property QtObject constants
    // 0 = snapping (zone layouts), 1 = tiling (autotile algorithms)
    property int viewMode: 0

    function getSlot(slotNumber) {
        return viewMode === 1 ? (kcm.getTilingQuickLayoutSlot(slotNumber) || "") : (kcm.getQuickLayoutSlot(slotNumber) || "");
    }

    function setSlot(slotNumber, value) {
        if (viewMode === 1)
            kcm.setTilingQuickLayoutSlot(slotNumber, value);
        else
            kcm.setQuickLayoutSlot(slotNumber, value);
    }

    header: Kirigami.Heading {
        level: 3
        text: root.viewMode === 1 ? i18n("Tiling Quick Shortcuts") : i18n("Quick Layout Shortcuts")
        padding: Kirigami.Units.smallSpacing
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            text: root.viewMode === 1 ? i18n("Assign tiling algorithms to keyboard shortcuts for instant switching.") : i18n("Assign layouts to keyboard shortcuts for instant switching.")
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

                required property int index
                property int slotNumber: index + 1
                property string shortcutText: root.kcm.getQuickLayoutShortcut(slotNumber)
                property int _slotRevision: 0

                width: ListView.view.width
                height: slotRow.implicitHeight + Kirigami.Units.smallSpacing * 2

                RowLayout {
                    id: slotRow

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    // Slot label + shortcut
                    ColumnLayout {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                        spacing: 0

                        Label {
                            text: root.viewMode === 1 ? i18n("Quick Tiling %1", slotDelegate.slotNumber) : i18n("Quick Layout %1", slotDelegate.slotNumber)
                        }

                        Label {
                            text: slotDelegate.shortcutText !== "" ? slotDelegate.shortcutText : i18n("No shortcut assigned")
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            opacity: slotDelegate.shortcutText !== "" ? 0.7 : 0.4
                        }

                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    // Layout selection
                    LayoutComboBox {
                        id: slotLayoutCombo

                        Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                        kcm: root.kcm
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

                            target: root.kcm
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
