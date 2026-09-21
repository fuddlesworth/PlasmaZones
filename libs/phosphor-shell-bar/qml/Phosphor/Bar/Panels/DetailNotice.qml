// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Rectangle {
    id: root
    property string text: ""
    property bool error: false
    readonly property color tint: error ? (Appearance.light ? "#9b233d" : "#ffa9b9") : Appearance.stops[0]
    visible: text !== ""
    width: parent ? parent.width : 0
    implicitHeight: layout.implicitHeight + 24
    radius: 9
    color: Qt.tint(Appearance.card, Qt.alpha(tint, 0.07))
    border.width: 1
    border.color: Qt.alpha(tint, 0.27)
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    onTextChanged: if (text)
        Accessible.announce(text, Accessible.Polite)
    RowLayout {
        id: layout
        x: 12
        y: 12
        width: Math.max(0, parent.width - 24)
        spacing: 8
        ShellIcon {
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            Layout.alignment: Qt.AlignTop
            source: root.error ? "dialog-warning-symbolic" : "checkmark"
            isMask: true
            color: root.tint
        }
        DetailText {
            Layout.fillWidth: true
            text: root.text
            size: 11
            lineHeight: 1.35
        }
    }
}
