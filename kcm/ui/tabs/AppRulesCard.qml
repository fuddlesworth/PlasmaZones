// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Card for managing app-to-zone auto-snap rules per layout.
 *
 * Lets users define rules like "firefox → zone 2" so windows auto-snap
 * to a specific zone when they open. Rules are per-layout.
 */
Kirigami.Card {
    id: root

    required property var kcm
    required property QtObject constants
    required property WindowPickerDialog windowPickerDialog

    // Current layout selection (populated from kcm.layouts)
    property string selectedLayoutId: ""
    property var currentRules: []
    property int selectedLayoutZoneCount: 99
    property bool pickerOpenedByUs: false
    // The effective layout ID: if "Default" is selected (empty), resolve to the global default
    readonly property string effectiveLayoutId: selectedLayoutId !== ""
        ? selectedLayoutId
        : (kcm ? kcm.defaultLayoutId : "")
    readonly property bool hasSelectedLayout: effectiveLayoutId !== ""

    Connections {
        target: root.kcm
        function onAppRulesRefreshed() {
            root.refreshRules()
        }
    }

    header: Kirigami.Heading {
        level: 3
        text: i18n("App-to-Zone Rules")
        padding: Kirigami.Units.smallSpacing
    }

    Connections {
        target: root.windowPickerDialog
        enabled: root.pickerOpenedByUs
        function onPicked(value) {
            patternField.text = value
            root.pickerOpenedByUs = false
        }
        function onClosed() {
            root.pickerOpenedByUs = false
        }
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        // Layout selector
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Layout:")
            }

            LayoutComboBox {
                id: layoutCombo
                Layout.fillWidth: true
                kcm: root.kcm
                noneText: i18n("Default")
                showPreview: true
                Accessible.name: i18n("Layout for app rules")

                onActivated: {
                    let selectedValue = model[currentIndex].value
                    root.selectedLayoutId = selectedValue
                    root.updateSelectedLayoutZoneCount()
                    root.refreshRules()
                }

                Component.onCompleted: {
                    root.updateSelectedLayoutZoneCount()
                    root.refreshRules()
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // Add rule row
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            enabled: root.hasSelectedLayout

            TextField {
                id: patternField
                Layout.fillWidth: true
                placeholderText: i18n("Window class pattern (e.g., firefox, org.kde.dolphin)")
                Accessible.name: i18n("Window class pattern")

                onAccepted: {
                    if (text.length > 0 && zoneSpinBox.value > 0) {
                        root.addRule(text, zoneSpinBox.value, screenCombo.currentValue)
                        text = ""
                    }
                }
            }

            Label {
                text: i18n("Zone:")
            }

            SpinBox {
                id: zoneSpinBox
                from: 1
                to: Math.max(1, root.selectedLayoutZoneCount)
                value: 1
                implicitWidth: Kirigami.Units.gridUnit * 5
                Accessible.name: i18n("Zone number")
            }

            ScreenComboBox {
                id: screenCombo
                kcm: root.kcm
                noneText: i18n("Any Screen")
                implicitWidth: Kirigami.Units.gridUnit * 12
                Accessible.name: i18n("Target screen")
            }

            Button {
                text: i18n("Add")
                icon.name: "list-add"
                enabled: patternField.text.length > 0
                ToolTip.text: i18n("Add app-to-zone rule")
                ToolTip.visible: hovered
                Accessible.name: i18n("Add app-to-zone rule")
                onClicked: {
                    root.addRule(patternField.text, zoneSpinBox.value, screenCombo.currentValue)
                    patternField.text = ""
                }
            }

            ToolButton {
                icon.name: "crosshairs"
                ToolTip.text: i18n("Pick from running windows")
                ToolTip.visible: hovered
                Accessible.name: i18n("Pick from running windows")

                onClicked: {
                    root.pickerOpenedByUs = true
                    root.windowPickerDialog.openForClasses()
                }
            }
        }

        // Rules list
        ListView {
            id: rulesListView
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(contentHeight, Kirigami.Units.gridUnit * 5)
            Layout.minimumHeight: Kirigami.Units.gridUnit * 5
            Layout.margins: Kirigami.Units.smallSpacing
            clip: true
            model: root.currentRules
            interactive: false
            opacity: root.hasSelectedLayout ? 1.0 : 0.5

            delegate: ItemDelegate {
                width: ListView.view.width
                required property var modelData
                required property int index

                contentItem: RowLayout {
                    Kirigami.Icon {
                        source: "application-x-executable"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Label {
                        text: modelData.pattern
                        Layout.alignment: Qt.AlignVCenter
                        elide: Text.ElideRight
                        Layout.maximumWidth: Math.min(implicitWidth, parent.width * 0.45)
                    }

                    Label {
                        text: modelData.targetScreen
                            ? i18n("→ Zone %1 on %2", modelData.zoneNumber, modelData.targetScreen)
                            : i18n("→ Zone %1", modelData.zoneNumber)
                        opacity: 0.7
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        icon.name: "edit-delete"
                        onClicked: root.removeRule(index)
                        Accessible.name: i18n("Remove rule for %1", modelData.pattern)
                    }
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: rulesListView.count === 0
                text: root.hasSelectedLayout
                    ? i18n("No app rules defined")
                    : i18n("No layouts available")
                explanation: root.hasSelectedLayout
                    ? i18n("Add rules above to auto-snap windows to specific zones when they open")
                    : i18n("Create a layout first to define app-to-zone rules")
            }
        }

        Kirigami.InlineMessage {
            id: duplicateWarning
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            type: Kirigami.MessageType.Warning
            text: i18n("A rule with that pattern already exists.")
            visible: false

            Timer {
                id: duplicateTimer
                interval: 3000
                onTriggered: duplicateWarning.visible = false
            }
        }

        Label {
            text: i18n("Rules are checked in order. The first matching pattern determines the zone. Patterns are case-insensitive substring matches.")
            font.italic: true
            opacity: 0.7
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
        }
    }

    function updateSelectedLayoutZoneCount() {
        let layoutId = effectiveLayoutId
        if (!kcm || !kcm.layouts || layoutId === "") {
            selectedLayoutZoneCount = 99
            return
        }
        for (let i = 0; i < kcm.layouts.length; ++i) {
            if (kcm.layouts[i].id === layoutId) {
                selectedLayoutZoneCount = kcm.layouts[i].zoneCount || 99
                return
            }
        }
        selectedLayoutZoneCount = 99
    }

    function refreshRules() {
        let layoutId = effectiveLayoutId
        if (layoutId !== "" && kcm) {
            currentRules = kcm.getAppRulesForLayout(layoutId)
        } else {
            currentRules = []
        }
    }

    function addRule(pattern, zoneNumber, targetScreen) {
        let trimmed = pattern.trim()
        if (trimmed.length === 0) {
            return
        }
        let layoutId = effectiveLayoutId
        if (layoutId === "" || !kcm) {
            return
        }
        let screen = targetScreen || ""
        // Check for duplicate: same pattern AND same targetScreen
        let lowerPattern = trimmed.toLowerCase()
        for (let i = 0; i < currentRules.length; ++i) {
            let rule = currentRules[i]
            if (rule.pattern.toLowerCase() === lowerPattern
                    && (rule.targetScreen || "") === screen) {
                duplicateWarning.visible = true
                duplicateTimer.restart()
                return
            }
        }
        kcm.addAppRuleToLayout(layoutId, trimmed, zoneNumber, screen)
        screenCombo.reset()
        refreshRules()
    }

    function removeRule(index) {
        let layoutId = effectiveLayoutId
        if (layoutId !== "" && kcm) {
            kcm.removeAppRuleFromLayout(layoutId, index)
            refreshRules()
        }
    }
}
