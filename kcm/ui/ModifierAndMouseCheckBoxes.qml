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
 */
Item {
    id: root

    readonly property int acceptModeAll: 0
    readonly property int acceptModeMetaOnly: 1
    readonly property int acceptModeMouseOnly: 2

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
    implicitWidth: field.implicitWidth
    implicitHeight: field.implicitHeight

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

    function clearAll() {
        if (modifierValue !== defaultModifierValue || mouseButtonValue !== defaultMouseButtonValue) {
            modifierValue = defaultModifierValue
            mouseButtonValue = defaultMouseButtonValue
            valueModified(defaultModifierValue)
            mouseButtonsModified(defaultMouseButtonValue)
        }
    }

    QQC2.TextField {
        id: field
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
