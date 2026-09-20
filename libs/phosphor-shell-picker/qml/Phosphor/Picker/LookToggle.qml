// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme

RowLayout {
    id: root
    property string label
    property string description: ""
    property string setting: ""
    property bool checked: setting ? Boolean(Appearance.settings[setting]) : false
    signal toggled(bool checked)
    spacing: 12
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 5
        LookText {
            text: root.label
            Layout.fillWidth: true
            size: 10
        }
        LookText {
            text: root.description
            visible: text.length > 0
            Layout.fillWidth: true
            size: 9
            muted: true
            lineHeight: 1.3
        }
    }
    Basic.Switch {
        id: toggle
        checked: root.checked
        implicitWidth: 34
        implicitHeight: 28
        padding: 0
        Accessible.name: root.label
        onToggled: {
            if (root.setting)
                AppearanceStore.setValue(root.setting, checked);
            root.toggled(checked);
        }
        indicator: Rectangle {
            x: 0
            y: 4
            width: 34
            height: 20
            radius: 10
            color: toggle.checked ? Qt.alpha(Appearance.accent, 0.5) : Appearance.recess
            border.color: toggle.visualFocus ? Appearance.text : Appearance.outline
            Rectangle {
                x: toggle.checked ? 18 : 3
                y: 3
                width: 14
                height: 14
                radius: 7
                color: toggle.checked ? Appearance.text : Appearance.muted
            }
        }
        contentItem: Item {}
    }
}
