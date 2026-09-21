// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Rectangle {
    id: root
    property string title: ""
    property string description: ""
    property string iconName: ""
    property string status: ""
    property bool compact: false
    property int tone: 0
    property int titleSize: 23
    property real padding: Appearance.compact ? 16 : 19
    default property alias content: extras.data
    width: parent ? parent.width : 0
    implicitHeight: layout.implicitHeight + padding * 2
    radius: Appearance.radius * 0.72
    border.width: 1
    border.color: Qt.tint(Appearance.outline, Qt.alpha(Appearance.stops[tone], 0.24))
    gradient: Gradient {
        GradientStop {
            position: 0
            color: Qt.tint(Appearance.card, Qt.alpha(Appearance.stops[root.tone], 0.12))
        }
        GradientStop {
            position: 1
            color: Qt.tint(Appearance.surface, Qt.alpha(Appearance.stops[1], 0.07))
        }
    }
    ColumnLayout {
        id: layout
        x: root.padding
        y: root.padding
        width: Math.max(0, root.width - root.padding * 2)
        spacing: root.compact ? 0 : 12
        RowLayout {
            Layout.fillWidth: true
            visible: root.iconName !== "" || root.status !== ""
            Rectangle {
                visible: root.iconName !== ""
                Layout.preferredWidth: root.compact ? 30 : 46
                Layout.preferredHeight: root.compact ? 30 : 46
                radius: width / 2
                color: Qt.alpha(Appearance.stops[root.tone], 0.09)
                border.width: 1
                border.color: Qt.alpha(Appearance.stops[root.tone], 0.3)
                ShellIcon {
                    anchors.centerIn: parent
                    width: root.compact ? 17 : 25
                    height: width
                    source: root.iconName
                    isMask: true
                    color: Appearance.stops[root.tone]
                }
            }
            DetailText {
                visible: root.compact
                Layout.fillWidth: true
                text: root.title
                size: 14
                font.weight: Font.Medium
            }
            Item {
                visible: !root.compact
                Layout.fillWidth: true
            }
            Rectangle {
                visible: root.status !== ""
                implicitWidth: statusText.implicitWidth + 18
                implicitHeight: statusText.implicitHeight + 10
                radius: height / 2
                color: Qt.alpha(Appearance.stops[0], 0.08)
                border.width: 1
                border.color: Qt.alpha(Appearance.stops[0], 0.25)
                DetailText {
                    id: statusText
                    anchors.centerIn: parent
                    text: root.status
                    size: 10
                }
            }
        }
        DetailText {
            visible: !root.compact && text !== ""
            Layout.fillWidth: true
            text: root.title
            size: root.titleSize
            font.weight: Font.Medium
            font.letterSpacing: -0.5
        }
        DetailText {
            visible: !root.compact && text !== ""
            Layout.fillWidth: true
            text: root.description
            size: 12
            muted: true
            lineHeight: 1.35
        }
        Column {
            id: extras
            visible: children.length > 0
            Layout.fillWidth: true
            spacing: 12
        }
    }
}
