// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Phosphor.Theme
import Phosphor.Widgets

Button {
    id: root
    property string title: ""
    property string subtitle: ""
    property string iconName: ""
    property string trailingText: ""
    property string trailingIcon: "go-next-symbolic"
    property bool selected: false
    property bool radio: false
    property bool grouped: false
    width: parent ? parent.width : 0
    implicitHeight: Math.max(Appearance.compact ? 58 : 66, row.implicitHeight + 24)
    Accessible.name: title + (subtitle ? ", " + subtitle : "")
    Accessible.role: radio ? Accessible.RadioButton : Accessible.Button
    Accessible.checked: selected
    background: Rectangle {
        color: root.down || root.selected ? Qt.alpha(Appearance.stops[0], 0.08) : root.hovered ? Qt.alpha(Appearance.stops[1], 0.09) : root.grouped ? "transparent" : Qt.alpha(Appearance.recess, 0.32)
        radius: Appearance.radius * 0.6
        border.width: root.grouped && !root.visualFocus ? 0 : 1
        border.color: root.visualFocus ? Appearance.text : Appearance.outline
    }
    contentItem: RowLayout {
        id: row
        spacing: 12
        Rectangle {
            Layout.leftMargin: 6
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: 9
            color: Qt.alpha(Appearance.stops[1], 0.1)
            ShellIcon {
                anchors.centerIn: parent
                width: 18
                height: 18
                source: root.iconName
                isMask: true
                color: Appearance.stops[1]
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            DetailText {
                Layout.fillWidth: true
                text: root.title
                size: 13
                font.weight: Font.Medium
            }
            DetailText {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.subtitle
                size: 10
                muted: true
            }
        }
        DetailText {
            visible: text !== ""
            text: root.trailingText
            size: 10
            muted: true
        }
        Rectangle {
            visible: root.radio
            Layout.rightMargin: 6
            Layout.preferredWidth: 19
            Layout.preferredHeight: 19
            radius: width / 2
            color: root.selected ? Qt.alpha(Appearance.stops[0], 0.2) : "transparent"
            border.width: 1
            border.color: root.selected ? Appearance.stops[0] : Appearance.muted
            ShellIcon {
                visible: root.selected
                anchors.centerIn: parent
                width: 13
                height: 13
                source: "checkmark"
                isMask: true
                color: Appearance.stops[0]
            }
        }
        ShellIcon {
            visible: !root.radio && root.trailingIcon !== ""
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13
            Layout.rightMargin: 6
            source: root.trailingIcon
            isMask: true
            color: Appearance.muted
        }
    }
}
