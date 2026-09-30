// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Basic.Button {
    id: root
    property string iconName: ""
    property string subtitle: ""
    property var statusEntry: null
    property bool selected: false
    property bool radio: false
    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(36, contentItem.implicitHeight + Tokens.spacing_s * 2)
    padding: Tokens.spacing_s
    leftPadding: Tokens.spacing_m
    rightPadding: Tokens.spacing_m
    Accessible.name: subtitle ? i18n("%1, %2").arg(text).arg(subtitle) : text
    Accessible.role: radio ? Accessible.RadioButton : checkable ? Accessible.CheckBox : Accessible.Button
    Accessible.checked: selected
    background: Rectangle {
        radius: Tokens.radius_s
        color: root.selected ? Qt.alpha(Appearance.stops[1], 0.12) : root.hovered || root.down ? Qt.alpha(Appearance.text, 0.08) : "transparent"
        border.width: root.visualFocus ? 1 : 0
        border.color: Appearance.text
    }
    contentItem: RowLayout {
        spacing: Tokens.spacing_m
        Loader {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            sourceComponent: root.statusEntry ? indicatorIcon : actionIcon
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacing_xs
            DetailText {
                Layout.fillWidth: true
                text: root.text
                color: root.enabled ? Appearance.text : Appearance.muted
                font.pixelSize: Tokens.font_size_body_s
            }
            DetailText {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.subtitle
                muted: true
                font.pixelSize: Tokens.font_size_label_s
            }
        }
    }
    Component {
        id: indicatorIcon
        StatusIcon {
            entry: root.statusEntry
        }
    }
    Component {
        id: actionIcon
        ShellIcon {
            source: root.selected ? "checkmark" : root.iconName
            isMask: true
            color: root.selected ? Appearance.stops[1] : Appearance.muted
        }
    }
}
