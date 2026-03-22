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
    property string formLabel
    property string tooltipText

    signal modifierSelected(int value)

    Kirigami.FormData.label: formLabel
    spacing: Kirigami.Units.smallSpacing

    ComboBox {
        id: combo

        readonly property var modifierOptions: [{
            "text": i18n("None"),
            "value": 0
        }, {
            "text": i18n("Shift"),
            "value": (1 << 25)
        }, {
            "text": i18n("Ctrl"),
            "value": (1 << 26)
        }, {
            "text": i18n("Alt"),
            "value": (1 << 27)
        }, {
            "text": i18n("Meta"),
            "value": (1 << 28)
        }]

        Layout.preferredWidth: Kirigami.Units.gridUnit * 10
        model: modifierOptions.map((o) => {
            return o.text;
        })
        currentIndex: {
            let val = root.modifierValue || 0;
            for (let i = 0; i < modifierOptions.length; i++) {
                if (modifierOptions[i].value === val)
                    return i;

            }
            return 0;
        }
        onActivated: (idx) => {
            root.modifierSelected(modifierOptions[idx].value);
        }
        ToolTip.visible: hovered
        ToolTip.text: root.tooltipText
    }

}
