// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

ColumnLayout {
    id: root
    property string label
    property string setting: ""
    property var choices: []
    property string value: setting ? String(Appearance.settings[setting]) : ""
    signal chosen(string value)
    spacing: 8
    LookText {
        text: root.label
        muted: true
        size: 10
    }
    ShellComboBox {
        id: input
        Layout.fillWidth: true
        implicitHeight: 35
        model: root.choices
        textRole: "label"
        valueRole: "value"
        currentIndex: Math.max(0, root.choices.findIndex(c => String(c.value) === root.value))
        Accessible.name: root.label
        onActivated: {
            const selected = root.choices[currentIndex].value;
            if (root.setting)
                AppearanceStore.setValue(root.setting, selected);
            root.chosen(String(selected));
        }
    }
}
