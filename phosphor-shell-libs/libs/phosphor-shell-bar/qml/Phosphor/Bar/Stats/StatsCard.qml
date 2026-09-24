// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

AbstractButton {
    id: root
    property string metric: "cpu"
    property string title: StatsModel.label(metric)
    property string status: ""
    property bool hero: false
    property real paddingSize: Appearance.compact ? 12 : 14
    default property alias body: content.data
    readonly property color tint: Appearance.stops[StatsModel.tone(metric)]
    implicitHeight: contentColumn.implicitHeight + paddingSize * 2
    Accessible.name: qsTr("View %1 details").arg(title)
    leftPadding: paddingSize
    rightPadding: paddingSize
    topPadding: paddingSize
    bottomPadding: paddingSize
    background: Rectangle {
        radius: Appearance.radius * 0.68
        border.width: 1
        border.color: root.visualFocus ? Appearance.text : root.hovered ? Qt.alpha(root.tint, 0.55) : root.hero ? Qt.tint(Appearance.outline, Qt.alpha(root.tint, 0.24)) : Appearance.outline
        gradient: Gradient {
            GradientStop {
                position: 0
                color: root.hero ? Qt.tint(Appearance.card, Qt.alpha(root.tint, 0.12)) : Qt.alpha(Appearance.card, 0.52)
            }
            GradientStop {
                position: 1
                color: Qt.alpha(Appearance.card, root.hovered ? 0.85 : 0.52)
            }
        }
    }
    contentItem: Column {
        id: contentColumn
        spacing: 12
        RowLayout {
            width: parent.width
            spacing: 8
            ShellIcon {
                source: StatsModel.icon(root.metric)
                isMask: true
                color: root.tint
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
            }
            DetailText {
                text: root.title
                size: 11
                font.weight: Font.Medium
                Layout.fillWidth: true
            }
            DetailText {
                text: root.status
                size: 9
                muted: true
                visible: text !== ""
            }
            ShellIcon {
                source: "go-next-symbolic"
                isMask: true
                color: Appearance.muted
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
            }
        }
        Column {
            id: content
            width: parent.width
            spacing: 10
        }
    }
}
