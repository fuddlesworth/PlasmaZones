// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Bold label + spacer + Switch for page-level enable toggles.
 *
 * Used at the top of settings pages to provide a master enable/disable
 * switch with a prominent label.
 */
RowLayout {
    id: root

    property string text
    property bool checked: false

    signal toggled(bool checked)

    Layout.fillWidth: true
    Layout.margins: Kirigami.Units.largeSpacing

    Label {
        text: root.text
        font.weight: Font.DemiBold
    }

    Item {
        Layout.fillWidth: true
    }

    SettingsSwitch {
        checked: root.checked
        onToggled: function(newValue) {
            root.toggled(newValue);
        }
        accessibleName: root.text
    }

}
