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
    required property var appSettings
    required property string iconSource
    required property string labelText
    // Optional customization
    property string noneText: i18n("Default")
    property int comboWidth: Kirigami.Units.gridUnit * 16
    property bool showPreview: true // Default to true for unified layout model with previews
    property real iconOpacity: 1
    property int layoutFilter: -1 // -1 = all, 0 = manual/zone only, 1 = autotile only
    // The layout ID that "Default" resolves to (set by parent based on hierarchy)
    property string resolvedDefaultId: appSettings ? appSettings.defaultLayoutId : ""
    // Current assignment (set externally, component updates selection)
    property string currentLayoutId: ""
    // Optional component injected between label and combo (e.g. enable Switch)
    property Component middleContent: null

    // Signals for assignment changes
    signal assignmentSelected(string layoutId)
    signal assignmentCleared()

    // Public function to clear selection programmatically
    function clearSelection() {
        layoutCombo.clearSelection();
    }

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
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        elide: Text.ElideRight
    }

    Loader {
        active: root.middleContent !== null
        sourceComponent: root.middleContent
        visible: active
    }

    LayoutComboBox {
        id: layoutCombo

        Layout.preferredWidth: root.comboWidth
        appSettings: root.appSettings
        noneText: root.noneText
        showPreview: root.showPreview
        layoutFilter: root.layoutFilter
        currentLayoutId: root.currentLayoutId
        resolvedDefaultId: root.resolvedDefaultId
        onActivated: {
            let selectedValue = model[currentIndex].value;
            if (selectedValue === "")
                root.assignmentCleared();
            else
                root.assignmentSelected(selectedValue);
        }
    }

    ToolButton {
        icon.name: "edit-clear"
        onClicked: {
            root.assignmentCleared();
            layoutCombo.clearSelection();
        }
        ToolTip.visible: hovered
        ToolTip.text: i18n("Clear assignment")
        Accessible.name: i18n("Clear assignment for %1", root.labelText)
    }

}
