// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme

ColumnLayout {
    id: root
    property string label
    property string setting
    property int minimum: 6
    property int maximum: 30
    property string unit: " px"
    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        LookText {
            text: root.label
            muted: true
            size: 10
            Layout.fillWidth: true
        }
        LookText {
            text: Math.round(slider.value) + root.unit
            size: 10
            font.family: Tokens.font_family_mono
        }
    }
    Basic.Slider {
        id: slider
        Layout.fillWidth: true
        implicitHeight: 25
        from: root.minimum
        to: root.maximum
        stepSize: 1
        value: Appearance.settings[root.setting]
        Accessible.name: root.label
        onMoved: if (!pressed)
            AppearanceStore.setValue(root.setting, Math.round(value))
        onPressedChanged: if (!pressed)
            AppearanceStore.setValue(root.setting, Math.round(value))
        background: Rectangle {
            x: slider.leftPadding
            y: (slider.height - height) / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Appearance.recess
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Appearance.accent
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: (slider.height - height) / 2
            width: 13
            height: 13
            radius: 7
            color: Appearance.text
            border.width: slider.visualFocus ? 2 : 0
            border.color: Appearance.accent
        }
    }
}
