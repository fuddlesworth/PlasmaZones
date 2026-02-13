// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Single input box for modifier and/or mouse trigger.
 * Uses TextField styling to match other form inputs (e.g. assignments tab).
 * Click in the box to capture; one key/modifier/mouse overwrites the value.
 * X inside the box clears. No pills; single value only.
 * acceptMode: MetaOnly (modifier keys only), MouseOnly (any mouse button: Right, Middle, Back, Forward, Extra 3–5), or All.
 *
 * Multi-bind mode (allowMultiple: true):
 * Shows a list of triggers with Add/Remove buttons. Each trigger is a separate
 * modifier or mouse button that can independently activate the feature.
 */
Item {
    id: root

    readonly property int acceptModeAll: 0
    readonly property int acceptModeMetaOnly: 1
    readonly property int acceptModeMouseOnly: 2

    // ═══════════════════════════════════════════════════════════════════════════
    // Single-mode properties (used when allowMultiple is false)
    // ═══════════════════════════════════════════════════════════════════════════
    property int modifierValue: 0
    property int mouseButtonValue: 0
    property int defaultModifierValue: 0
    property int defaultMouseButtonValue: 0
    property int acceptMode: acceptModeAll
    property bool tooltipEnabled: true
    /** When set, overrides the default tooltip for the input field (use instead of ToolTip.text; Item has no ToolTip attached type). */
    property string customTooltipText: ""

    signal valueModified(int modifierValue)
    signal mouseButtonsModified(int mouseButtonValue)

    // ═══════════════════════════════════════════════════════════════════════════
    // Multi-mode properties (used when allowMultiple is true)
    // ═══════════════════════════════════════════════════════════════════════════
    property bool allowMultiple: false
    property var triggers: []       // [{modifier: bitmask, mouseButton: buttonBit}, ...]
    property var defaultTriggers: []
    signal triggersModified(var triggers)

    readonly property int maxTriggers: 4

    readonly property int shiftFlag: 0x02000000
    readonly property int ctrlFlag: 0x04000000
    readonly property int altFlag: 0x08000000
    readonly property int metaFlag: 0x10000000

    readonly property var modifierChips: [
        { bit: shiftFlag, label: i18n("Shift") },
        { bit: ctrlFlag, label: i18n("Ctrl") },
        { bit: altFlag, label: i18n("Alt") },
        { bit: metaFlag, label: i18n("Meta") }
    ]
    // Qt::MouseButton bits; UI labels "Extra 3/4/5" match common naming (kcfg uses Extra1/2/3 for 32/64/128)
    readonly property var mouseButtonList: [
        { bit: 0x02, label: i18n("Right") },
        { bit: 0x04, label: i18n("Middle") },
        { bit: 0x08, label: i18n("Back") },
        { bit: 0x10, label: i18n("Forward") },
        { bit: 0x20, label: i18n("Extra 3") },
        { bit: 0x40, label: i18n("Extra 4") },
        { bit: 0x80, label: i18n("Extra 5") }
    ]

    // Match KeySequenceInput: no fixed width so FormLayout gives same column width as shortcut fields
    implicitWidth: allowMultiple ? multiContainer.implicitWidth : field.implicitWidth
    implicitHeight: allowMultiple ? multiContainer.implicitHeight : field.implicitHeight

    function displayText() {
        var parts = []
        if (acceptMode !== acceptModeMouseOnly) {
            for (var i = 0; i < modifierChips.length; i++)
                if ((modifierValue & modifierChips[i].bit) !== 0)
                    parts.push(modifierChips[i].label)
        }
        if (acceptMode !== acceptModeMetaOnly) {
            for (var j = 0; j < mouseButtonList.length; j++)
                if ((mouseButtonValue & mouseButtonList[j].bit) !== 0)
                    parts.push(mouseButtonList[j].label)
        }
        if (parts.length === 0)
            return ""
        return parts.join(" + ")
    }

    /** Display text for a single trigger (modifier bitmask + mouse button bit) */
    function triggerDisplayText(modifier, mouseButton) {
        var parts = []
        if (acceptMode !== acceptModeMouseOnly) {
            for (var i = 0; i < modifierChips.length; i++)
                if ((modifier & modifierChips[i].bit) !== 0)
                    parts.push(modifierChips[i].label)
        }
        if (acceptMode !== acceptModeMetaOnly) {
            for (var j = 0; j < mouseButtonList.length; j++)
                if ((mouseButton & mouseButtonList[j].bit) !== 0)
                    parts.push(mouseButtonList[j].label)
        }
        if (parts.length === 0)
            return i18n("(none)")
        return parts.join(" + ")
    }

    function clearAll() {
        if (modifierValue !== defaultModifierValue || mouseButtonValue !== defaultMouseButtonValue) {
            modifierValue = defaultModifierValue
            mouseButtonValue = defaultMouseButtonValue
            valueModified(defaultModifierValue)
            mouseButtonsModified(defaultMouseButtonValue)
        }
    }

    /** Compare two trigger arrays for equality */
    function triggersEqual(a, b) {
        if (!a || !b || a.length !== b.length) return false
        for (var i = 0; i < a.length; i++) {
            if ((a[i].modifier || 0) !== (b[i].modifier || 0)
                || (a[i].mouseButton || 0) !== (b[i].mouseButton || 0))
                return false
        }
        return true
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Multi-mode: list of triggers with Add/Remove
    // ═══════════════════════════════════════════════════════════════════════════
    Rectangle {
        id: multiContainer
        visible: root.allowMultiple
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
        border.color: Kirigami.Theme.disabledTextColor
        border.width: 1
        radius: Kirigami.Units.smallSpacing

        implicitWidth: multiLayout.implicitWidth + Kirigami.Units.smallSpacing * 2
        implicitHeight: multiLayout.implicitHeight + Kirigami.Units.smallSpacing * 2

        ColumnLayout {
            id: multiLayout
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            spacing: 2

            // Trigger rows
            Repeater {
                model: root.allowMultiple ? root.triggers : []

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        text: root.triggerDisplayText(modelData.modifier || 0, modelData.mouseButton || 0)
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    QQC2.ToolButton {
                        icon.name: "list-remove"
                        Accessible.name: i18n("Remove trigger")
                        implicitWidth: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing * 2
                        implicitHeight: implicitWidth
                        enabled: root.triggers.length > 1
                        onClicked: {
                            var newTriggers = []
                            for (var i = 0; i < root.triggers.length; i++) {
                                if (i !== index)
                                    newTriggers.push(root.triggers[i])
                            }
                            root.triggersModified(newTriggers)
                        }
                        QQC2.ToolTip.visible: hovered && root.tooltipEnabled
                        QQC2.ToolTip.text: root.triggers.length <= 1
                            ? i18n("At least one trigger is required")
                            : i18n("Remove this trigger")
                    }
                }
            }

            // Add and Reset row
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.ToolButton {
                    text: i18n("Add…")
                    icon.name: "list-add"
                    enabled: root.triggers.length < root.maxTriggers
                    onClicked: multiInputCapture.startCapture()
                    QQC2.ToolTip.visible: hovered && root.tooltipEnabled
                    QQC2.ToolTip.text: i18n("Add another activation trigger")
                }

                Item { Layout.fillWidth: true }

                QQC2.ToolButton {
                    icon.name: "edit-clear"
                    Accessible.name: i18n("Reset to default")
                    visible: !triggersEqual(root.triggers, root.defaultTriggers)
                    onClicked: {
                        var copy = []
                        for (var i = 0; i < root.defaultTriggers.length; i++)
                            copy.push({modifier: root.defaultTriggers[i].modifier || 0,
                                       mouseButton: root.defaultTriggers[i].mouseButton || 0})
                        root.triggersModified(copy)
                    }
                    QQC2.ToolTip.visible: hovered && root.tooltipEnabled
                    QQC2.ToolTip.text: i18n("Reset to default")
                }
            }
        }
    }

    // InputCapture for multi-mode (adds to list instead of replacing)
    InputCapture {
        id: multiInputCapture
        visible: false
        acceptMode: root.acceptMode
        tooltipEnabled: root.tooltipEnabled
        onModifierCaptured: (mask) => {
            if (!root.allowMultiple) return
            // Check for duplicate
            for (var i = 0; i < root.triggers.length; i++) {
                if ((root.triggers[i].modifier || 0) === mask && (root.triggers[i].mouseButton || 0) === 0)
                    return
            }
            var newTriggers = []
            for (var j = 0; j < root.triggers.length; j++)
                newTriggers.push(root.triggers[j])
            newTriggers.push({modifier: mask, mouseButton: 0})
            root.triggersModified(newTriggers)
        }
        onMouseCaptured: (bit) => {
            if (!root.allowMultiple) return
            if (root.acceptMode === root.acceptModeMetaOnly) return
            // Check for duplicate
            for (var i = 0; i < root.triggers.length; i++) {
                if ((root.triggers[i].modifier || 0) === 0 && (root.triggers[i].mouseButton || 0) === bit)
                    return
            }
            var newTriggers = []
            for (var j = 0; j < root.triggers.length; j++)
                newTriggers.push(root.triggers[j])
            newTriggers.push({modifier: 0, mouseButton: bit})
            root.triggersModified(newTriggers)
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Single-mode: existing TextField input
    // ═══════════════════════════════════════════════════════════════════════════
    QQC2.TextField {
        id: field
        visible: !root.allowMultiple
        anchors.fill: parent
        readOnly: true
        text: root.displayText()
        placeholderText: i18n("Click to set shortcut")
        rightPadding: clearBtn.visible ? (clearBtn.width + Kirigami.Units.smallSpacing * 2) : (leftPadding + Kirigami.Units.smallSpacing)

        color: inputCapture.capturing ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
        font.italic: inputCapture.capturing
        placeholderTextColor: Kirigami.Theme.disabledTextColor

        MouseArea {
            id: clickArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor
            z: 10
            onClicked: (mouse) => {
                if (clearBtn.visible && mouse.x >= field.width - clearBtn.width - Kirigami.Units.smallSpacing * 2) {
                    root.clearAll()
                    return
                }
                inputCapture.startCapture()
            }
        }

        background: Rectangle {
            color: inputCapture.capturing ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2) : (field.enabled ? Kirigami.Theme.backgroundColor : Qt.alpha(Kirigami.Theme.backgroundColor, 0.5))
            border.color: inputCapture.capturing ? Kirigami.Theme.highlightColor : (field.activeFocus ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor)
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }

        QQC2.Button {
            id: clearBtn
            visible: !inputCapture.capturing && (root.modifierValue !== root.defaultModifierValue || root.mouseButtonValue !== root.defaultMouseButtonValue)
            anchors.right: parent.right
            anchors.rightMargin: Kirigami.Units.smallSpacing
            anchors.verticalCenter: parent.verticalCenter
            width: height
            height: parent.height - Kirigami.Units.smallSpacing * 2
            flat: true
            icon.name: "edit-clear"
            z: 1
            Accessible.role: Accessible.Button
            Accessible.name: i18n("Reset to default")
            onClicked: root.clearAll()
            QQC2.ToolTip.visible: hovered && root.tooltipEnabled
            QQC2.ToolTip.text: i18n("Reset to default")
        }

        // ToolTip attached to Control (TextField) - Item has no ToolTip attached type
        QQC2.ToolTip.visible: root.tooltipEnabled && clickArea.containsMouse && !inputCapture.capturing
        QQC2.ToolTip.text: root.customTooltipText !== "" ? root.customTooltipText
            : (root.acceptMode === root.acceptModeMetaOnly ? i18n("Click to set modifier key(s)")
              : (root.acceptMode === root.acceptModeMouseOnly ? i18n("Click to set any mouse button (Right, Middle, Back, Forward, etc.)")
                : i18n("Click to set key, modifier, or any mouse button")))
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    }

    InputCapture {
        id: inputCapture
        visible: false
        acceptMode: root.acceptMode
        tooltipEnabled: root.tooltipEnabled
        onModifierCaptured: (mask) => {
            root.modifierValue = mask
            root.mouseButtonValue = 0
            root.valueModified(mask)
            root.mouseButtonsModified(0)
        }
        onMouseCaptured: (bit) => {
            if (root.acceptMode === root.acceptModeMetaOnly)
                return
            root.modifierValue = 0
            root.mouseButtonValue = bit
            root.valueModified(0)
            root.mouseButtonsModified(bit)
        }
    }
}
