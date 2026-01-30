// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable row for layout assignment with icon, label, combo box, and clear button
 *
 * This component eliminates the duplicated assignment row pattern across
 * MonitorAssignmentsCard, ActivityAssignmentsCard, and QuickLayoutSlotsCard.
 *
 * Single Responsibility: Display and manage a single layout assignment.
 */
RowLayout {
    id: root

    // Required properties
    required property var kcm
    required property string iconSource
    required property string labelText

    // Optional customization
    property string noneText: i18n("Default")
    property int labelWidth: Kirigami.Units.gridUnit * 8
    property int comboWidth: Kirigami.Units.gridUnit * 12
    property bool showPreview: false
    property real iconOpacity: 1.0

    // Current assignment (set externally, component updates selection)
    property string currentLayoutId: ""

    // Signals for assignment changes
    signal assignmentSelected(string layoutId)
    signal assignmentCleared()

    spacing: Kirigami.Units.smallSpacing

    Kirigami.Icon {
        source: root.iconSource
        Layout.preferredWidth: Kirigami.Units.iconSizes.small
        Layout.preferredHeight: Kirigami.Units.iconSizes.small
        Layout.alignment: Qt.AlignVCenter
        opacity: root.iconOpacity
    }

    Label {
        text: root.labelText
        Layout.preferredWidth: root.labelWidth
        Layout.alignment: Qt.AlignVCenter
        elide: Text.ElideRight
    }

    LayoutComboBox {
        id: layoutCombo
        Layout.preferredWidth: root.comboWidth
        kcm: root.kcm
        noneText: root.noneText
        showPreview: root.showPreview
        currentLayoutId: root.currentLayoutId

        onActivated: {
            let selectedValue = model[currentIndex].value
            if (selectedValue === "") {
                root.assignmentCleared()
            } else {
                root.assignmentSelected(selectedValue)
            }
        }
    }

    ToolButton {
        icon.name: "edit-clear"
        onClicked: {
            root.assignmentCleared()
            layoutCombo.clearSelection()
        }
        ToolTip.visible: hovered
        ToolTip.text: i18n("Clear assignment")
        Accessible.name: i18n("Clear assignment for %1", root.labelText)
    }

    Item { Layout.fillWidth: true }

    // Public function to clear selection programmatically
    function clearSelection() {
        layoutCombo.clearSelection()
    }
}
