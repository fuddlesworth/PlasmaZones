// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Checkbox group for selecting multiple modifier keys
 * Stores as Qt::KeyboardModifier bitmask (int)
 */
RowLayout {
    // Qt.MetaModifier

    id: root

    property int modifierValue: 0 // Qt::KeyboardModifier bitmask
    property bool showPreview: true // Show active combination label
    property bool tooltipEnabled: true // Allow tooltips to be shown (set to false when tab is inactive)
    // Qt::KeyboardModifier flag values (exact, not float approximations)
    readonly property int shiftFlag: 0x02000000 // Qt::ShiftModifier
    readonly property int ctrlFlag: 0x04000000 // Qt::ControlModifier
    readonly property int altFlag: 0x08000000 // Qt::AltModifier
    readonly property int metaFlag: 0x10000000 // Qt::MetaModifier

    signal valueModified(int value) // Signal when user changes the value (not property change)

    // Helper function to build modifier combination string
    function getModifierString(value) {
        if (value === 0)
            return i18n("None");

        var parts = [];
        if ((value & shiftFlag) !== 0)
            parts.push(i18n("Shift"));

        if ((value & ctrlFlag) !== 0)
            parts.push(i18n("Ctrl"));

        if ((value & altFlag) !== 0)
            parts.push(i18n("Alt"));

        if ((value & metaFlag) !== 0)
            parts.push(i18n("Meta"));

        return parts.join(" + ");
    }

    // Helper function to calculate bitmask from checkboxes
    function calculateModifierValue() {
        var value = 0;
        if (shiftCheck.checked)
            value |= shiftFlag;

        if (ctrlCheck.checked)
            value |= ctrlFlag;

        if (altCheck.checked)
            value |= altFlag;

        if (metaCheck.checked)
            value |= metaFlag;

        return value;
    }

    spacing: Kirigami.Units.smallSpacing
    // Update checkboxes when modifierValue changes externally
    Component.onCompleted: {
        // Initial sync
        shiftCheck.checked = (modifierValue & shiftFlag) !== 0;
        ctrlCheck.checked = (modifierValue & ctrlFlag) !== 0;
        altCheck.checked = (modifierValue & altFlag) !== 0;
        metaCheck.checked = (modifierValue & metaFlag) !== 0;
    }
    onModifierValueChanged: {
        // Sync checkboxes when value changes externally (without triggering our signal)
        var shiftShouldBe = (modifierValue & shiftFlag) !== 0;
        var ctrlShouldBe = (modifierValue & ctrlFlag) !== 0;
        var altShouldBe = (modifierValue & altFlag) !== 0;
        var metaShouldBe = (modifierValue & metaFlag) !== 0;
        if (shiftCheck.checked !== shiftShouldBe)
            shiftCheck.checked = shiftShouldBe;

        if (ctrlCheck.checked !== ctrlShouldBe)
            ctrlCheck.checked = ctrlShouldBe;

        if (altCheck.checked !== altShouldBe)
            altCheck.checked = altShouldBe;

        if (metaCheck.checked !== metaShouldBe)
            metaCheck.checked = metaShouldBe;

    }

    CheckBox {
        id: shiftCheck

        text: i18n("Shift")
        checked: (root.modifierValue & root.shiftFlag) !== 0
        onCheckedChanged: {
            var newValue = root.calculateModifierValue();
            if (newValue !== root.modifierValue) {
                root.modifierValue = newValue;
                root.valueModified(newValue);
            }
        }
        ToolTip.visible: hovered && root.tooltipEnabled
        ToolTip.text: i18n("Shift modifier key")
    }

    CheckBox {
        id: ctrlCheck

        text: i18n("Ctrl")
        checked: (root.modifierValue & root.ctrlFlag) !== 0
        onCheckedChanged: {
            var newValue = root.calculateModifierValue();
            if (newValue !== root.modifierValue) {
                root.modifierValue = newValue;
                root.valueModified(newValue);
            }
        }
        ToolTip.visible: hovered && root.tooltipEnabled
        ToolTip.text: i18n("Control modifier key")
    }

    CheckBox {
        id: altCheck

        text: i18n("Alt")
        checked: (root.modifierValue & root.altFlag) !== 0
        onCheckedChanged: {
            var newValue = root.calculateModifierValue();
            if (newValue !== root.modifierValue) {
                root.modifierValue = newValue;
                root.valueModified(newValue);
            }
        }
        ToolTip.visible: hovered && root.tooltipEnabled
        ToolTip.text: i18n("Alt modifier key")
    }

    CheckBox {
        id: metaCheck

        text: i18n("Meta")
        checked: (root.modifierValue & root.metaFlag) !== 0
        onCheckedChanged: {
            var newValue = root.calculateModifierValue();
            if (newValue !== root.modifierValue) {
                root.modifierValue = newValue;
                root.valueModified(newValue);
            }
        }
        ToolTip.visible: hovered && root.tooltipEnabled
        ToolTip.text: i18n("Meta/Super modifier key")
    }

    // Preview label showing active combination
    Label {
        Layout.leftMargin: Kirigami.Units.smallSpacing
        text: root.showPreview ? i18n("Active: %1", root.getModifierString(root.modifierValue)) : ""
        opacity: 0.7
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
        visible: root.showPreview
    }

}
