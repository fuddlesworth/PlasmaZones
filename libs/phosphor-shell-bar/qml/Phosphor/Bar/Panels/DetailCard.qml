// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Rectangle {
    id: root
    property string title: ""
    property string kicker: ""
    property bool showCompactTitle: true
    property string description: ""
    property string iconName: ""
    property string iconFallback: "application-x-executable"
    property string status: ""
    property bool compact: false
    property bool horizontal: false
    property int tone: 0
    property int titleSize: 23
    property real padding: Appearance.compact ? 16 : 19
    readonly property real contentPadding: compact ? 13 : padding
    default property alias content: extras.data
    width: parent ? parent.width : 0
    implicitHeight: layout.implicitHeight + contentPadding * 2
    radius: Appearance.radius * 0.72
    border.width: 1
    border.color: horizontal ? Appearance.outline : Qt.tint(Appearance.outline, Qt.alpha(Appearance.stops[tone], 0.24))
    gradient: Gradient {
        GradientStop {
            position: 0
            color: root.horizontal ? Qt.alpha(Appearance.card, 0.48) : Qt.tint(Appearance.card, Qt.alpha(Appearance.stops[root.tone], 0.12))
        }
        GradientStop {
            position: 1
            color: root.horizontal ? Qt.alpha(Appearance.card, 0.48) : Qt.tint(Appearance.surface, Qt.alpha(Appearance.stops[1], 0.07))
        }
    }
    ColumnLayout {
        id: layout
        x: root.contentPadding
        y: root.contentPadding
        width: Math.max(0, root.width - root.contentPadding * 2)
        spacing: root.compact ? 0 : 12
        RowLayout {
            Layout.fillWidth: true
            visible: root.iconName !== "" || root.status !== ""
            Rectangle {
                visible: root.iconName !== ""
                Layout.preferredWidth: root.compact ? 30 : root.horizontal ? 40 : 46
                Layout.preferredHeight: Layout.preferredWidth
                radius: root.horizontal ? 10 : width / 2
                color: Qt.alpha(Appearance.stops[root.tone], 0.09)
                border.width: root.horizontal ? 0 : 1
                border.color: Qt.alpha(Appearance.stops[root.tone], 0.3)
                ShellIcon {
                    anchors.centerIn: parent
                    width: root.compact ? 17 : 25
                    height: width
                    source: root.iconName
                    fallback: root.iconFallback
                    isMask: true
                    color: Appearance.stops[root.tone]
                }
            }
            DetailText {
                visible: root.compact && !root.horizontal && root.showCompactTitle
                Layout.fillWidth: true
                text: root.title
                size: 14
                font.weight: Font.Medium
            }
            Item {
                visible: !root.horizontal && (!root.compact || !root.showCompactTitle)
                Layout.fillWidth: true
            }
            ColumnLayout {
                visible: root.horizontal
                Layout.fillWidth: true
                spacing: 3
                DetailText {
                    Layout.fillWidth: true
                    text: root.title
                    size: root.titleSize
                    font.weight: Font.Medium
                }
                DetailText {
                    visible: text !== ""
                    Layout.fillWidth: true
                    text: root.description
                    size: 10
                    muted: true
                }
            }
            Rectangle {
                visible: !root.horizontal && root.status !== ""
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
            DetailText {
                visible: root.horizontal && root.status !== ""
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 3
                text: root.status
                size: 9
                muted: true
            }
        }
        DetailText {
            visible: !root.compact && !root.horizontal && text !== ""
            Layout.fillWidth: true
            text: root.kicker
            size: 9
            muted: true
            font.letterSpacing: 1.5
        }
        DetailText {
            visible: !root.compact && !root.horizontal && text !== ""
            Layout.fillWidth: true
            text: root.title
            size: root.titleSize
            font.weight: Font.Medium
            font.letterSpacing: -0.5
        }
        DetailText {
            visible: !root.compact && !root.horizontal && text !== ""
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
