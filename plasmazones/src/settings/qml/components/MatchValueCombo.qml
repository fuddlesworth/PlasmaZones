// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls

/**
 * @brief The dropdown a rule condition uses when its value is picked from a
 *        list: a monitor, an activity, a desktop, a layout, a window type, a
 *        placement mode, an orientation or a colour scheme.
 *
 * The host passes the list as `model` (an array of JS objects), names the
 * label and value keys with `textRole` / `valueRole`, and binds `value` to the
 * stored wire value. The selection is DERIVED from `value` by walking the
 * model, never stored, so the combo follows an external edit and a list that
 * republishes under it. A stored value the list does not hold (an unplugged
 * monitor, a deleted layout, a token from a newer build) is shown raw rather
 * than as a blank combo, and an unset one shows `placeholderText`.
 *
 * String values match ignoring case, as the rule engine's Equals does, so a
 * hand-edited id in another case still selects its entry. `picked(value)`
 * fires only when the user picks a value other than the stored one.
 */
WideComboBox {
    id: combo

    /// The stored wire value. Unset is undefined, null or "".
    property var value
    /// Shown while no value is set.
    property string placeholderText

    signal picked(var value)

    readonly property bool _hasValue: combo.value !== undefined && combo.value !== null && combo.value !== ""

    function _same(a, b) {
        if (typeof a === "string" && typeof b === "string")
            return a.toLowerCase() === b.toLowerCase();

        return a === b;
    }

    currentIndex: {
        const items = combo.model || [];
        for (let i = 0; i < items.length; ++i) {
            if (items[i] && combo._same(items[i][combo.valueRole], combo.value))
                return i;
        }
        return -1;
    }
    displayText: combo.currentIndex >= 0 ? combo.currentText : (combo._hasValue ? String(combo.value) : combo.placeholderText)
    onActivated: function (index) {
        if (!combo._same(combo.currentValue, combo.value))
            combo.picked(combo.currentValue);
    }
}
