// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    required property int index
    required property string title
    required property string subtitle
    required property string iconName
    required property string primaryActionLabel
    required property string alternateActionLabel
    required property bool hasAlternateAction
    property string resultId: ""
    property string providerId: ""
    property var catalog: null
    property bool current: false
    property bool compact: false
    signal clicked
    implicitHeight: compact ? 53 : 59
    Accessible.role: Accessible.ListItem
    Accessible.name: root.subtitle.length ? qsTr("%1, %2").arg(root.title).arg(root.subtitle) : root.title
    Accessible.selected: current
    Accessible.onPressAction: root.clicked()

    function appIcon(name: string): string {
        const id = name.toLowerCase();
        if (/firefox|chrom|browser/.test(id))
            return "internet-web-browser";
        if (/dolphin|folder|files/.test(id))
            return "folder";
        if (/kate|konsole|terminal|code/.test(id))
            return "utilities-terminal";
        return name;
    }
    Rectangle {
        anchors.fill: parent
        radius: Math.min(9, Appearance.radius)
        color: root.current ? Qt.tint(Appearance.recess, Qt.alpha(Appearance.accent, 0.18)) : hover.hovered ? Appearance.card : "transparent"
    }
    Rectangle {
        visible: root.current
        width: 2
        height: parent.height - 8
        y: 4
        radius: 1
        color: Appearance.text
    }
    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: root.clicked()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: root.catalog !== null && root.providerId === "apps"
        onTapped: pinMenu.popup()
    }
    Menu {
        id: pinMenu
        MenuItem {
            text: root.catalog && root.catalog.isPinned(root.resultId) ? qsTr("Unpin from launcher") : qsTr("Pin to launcher")
            onTriggered: root.catalog.togglePinned(root.resultId)
        }
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 10 : 14
        anchors.rightMargin: root.compact ? 10 : 14
        spacing: 14
        ShellIcon {
            source: root.appIcon(root.iconName)
            implicitWidth: 18
            implicitHeight: 18
            color: Appearance.accent
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: root.title
                textFormat: Text.PlainText
                color: Appearance.text
                font.family: Tokens.font_family_ui
                font.pixelSize: Math.round((12) * Appearance.textScale)
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: root.subtitle.length > 0
                text: root.subtitle
                textFormat: Text.PlainText
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: Math.round((10) * Appearance.textScale)
                elide: Text.ElideRight
            }
        }
        Keycap {
            visible: root.current
            text: "↵"
        }
    }
}
