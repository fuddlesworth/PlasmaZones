// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme

Item {
    id: root
    required property var entry
    property bool selected: false
    signal menuRequested(var entry, Item anchor)
    signal activated
    implicitHeight: Appearance.compact ? 98 : 110
    Rectangle {
        anchors.fill: parent
        radius: Appearance.radius * 0.6
        color: root.selected ? Qt.tint(Appearance.card, Qt.alpha(Appearance.accent, 0.13)) : launch.hovered || launch.activeFocus ? Qt.alpha(Appearance.card, 0.65) : "transparent"
        border.width: root.selected || launch.hovered || launch.activeFocus ? 1 : 0
        border.color: launch.visualFocus ? Appearance.text : root.selected ? Qt.alpha(Appearance.accent, 0.45) : Appearance.outline
    }
    Basic.AbstractButton {
        id: launch
        objectName: "trayApp-" + root.entry.preferenceKey
        anchors.fill: parent
        Accessible.name: TrayModel.title(root.entry)
        Accessible.description: TrayModel.status(root.entry)
        contentItem: Column {
            topPadding: 15
            spacing: 10
            TrayIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 27
                height: 29
                entry: root.entry
                tint: Appearance.text
            }
            DetailText {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: TrayModel.title(root.entry)
                size: 12
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            DetailText {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: root.entry.attention ? qsTr("Needs attention") : ""
                color: Appearance.stops[3]
                size: 8
                maximumLineCount: 1
                elide: Text.ElideRight
            }
        }
        onClicked: {
            if (root.entry.itemIsMenu)
                root.menuRequested(root.entry, root);
            else {
                const entry = root.entry;
                const point = root.mapToGlobal(width / 2, height / 2);
                root.activated();
                Qt.callLater(() => TrayModel.primary(entry, point));
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: root.menuRequested(root.entry, root)
        }
        TapHandler {
            acceptedButtons: Qt.MiddleButton
            onTapped: TrayModel.secondary(root.entry, root.mapToGlobal(root.width / 2, root.height / 2))
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Menu || event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier) {
                root.menuRequested(root.entry, root);
                event.accepted = true;
            }
        }
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        Basic.ToolTip.visible: hovered
        Basic.ToolTip.delay: 700
        Basic.ToolTip.text: TrayModel.title(root.entry) + "\n" + TrayModel.status(root.entry)
    }
    TrayButton {
        anchors.right: parent.right
        anchors.top: parent.top
        width: 23
        height: 22
        iconName: "view-more-symbolic"
        label: qsTr("%1 menu").arg(TrayModel.title(root.entry))
        onClicked: root.menuRequested(root.entry, root)
    }
    function focusPrimary(): void {
        launch.forceActiveFocus();
    }
}
