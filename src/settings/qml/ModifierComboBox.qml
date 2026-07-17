// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief A reusable modifier key ComboBox for Kirigami.FormLayout.
 *
 * Presents a dropdown with None, Shift, Ctrl, Alt, Meta options
 * and maps between Qt::KeyboardModifier bit values and list indices.
 */
RowLayout {
    id: root

    property int modifierValue: 0
    property string tooltipText
    //* @brief Screen-reader name for the INNER ComboBox (the focusable control).
    //* Setting Accessible.name on this RowLayout wrapper never reaches the
    //* ComboBox, so callers use this instead (mirrors SettingsSlider).
    property string accessibleName: ""

    signal modifierSelected(int value)

    spacing: Kirigami.Units.smallSpacing

    ComboBox {
        id: combo

        readonly property var modifierOptions: [
            {
                "text": i18n("None"),
                "value": 0
            },
            {
                "text": i18n("Shift"),
                "value": (1 << 25)
            },
            {
                "text": i18n("Ctrl"),
                "value": (1 << 26)
            },
            {
                "text": i18n("Alt"),
                "value": (1 << 27)
            },
            {
                "text": i18n("Meta"),
                "value": (1 << 28)
            }
        ]

        Accessible.name: root.accessibleName
        Layout.preferredWidth: Kirigami.Units.gridUnit * 10
        model: modifierOptions.map(o => {
            return o.text;
        })
        // Guarded binding: a plain `currentIndex:` binding is severed the
        // first time the user activates an item, after which external
        // modifierValue changes no longer update the combo.
        Binding on currentIndex {
            value: {
                let val = root.modifierValue || 0;
                for (let i = 0; i < combo.modifierOptions.length; i++) {
                    if (combo.modifierOptions[i].value === val)
                        return i;
                }
                return 0;
            }
            when: !combo.popup.visible
            restoreMode: Binding.RestoreNone
        }
        onActivated: idx => {
            root.modifierSelected(modifierOptions[idx].value);
        }
        ToolTip.visible: hovered && root.tooltipText.length > 0
        ToolTip.text: root.tooltipText
    }
}
