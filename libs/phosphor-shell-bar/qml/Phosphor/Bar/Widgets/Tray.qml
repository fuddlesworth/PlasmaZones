// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic
import Phosphor.Theme
import Phosphor.Widgets

BarWidget {
    id: root
    signal activated
    property real railT: 0.8
    property bool expanded: false
    property real maximumWidth: 10000
    property string requestedMenuKey: ""
    property Item requestAnchor: null
    readonly property int capacity: Math.max(0, Math.min(Appearance.settings.trayLimit, Math.floor((maximumWidth - 44) / 31)))
    readonly property var shownItems: TrayModel.barItems.slice(0, capacity)
    contentWidth: icons.implicitWidth + 12
    contentHeight: 32
    function menu(entry, anchor: Item): void {
        requestedMenuKey = entry.instanceKey;
        requestAnchor = anchor;
        activated();
    }
    Rectangle {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: 28
        color: Appearance.outline
    }
    Rectangle {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: 28
        color: Appearance.outline
    }
    Row {
        id: icons
        x: 6
        spacing: 2
        Repeater {
            model: root.shownItems
            delegate: AbstractButton {
                id: appButton
                required property var modelData
                width: 29
                height: 32
                Accessible.name: TrayModel.title(modelData)
                Accessible.description: TrayModel.status(modelData)
                background: Rectangle {
                    radius: 7
                    color: appButton.hovered || appButton.visualFocus ? Appearance.card : "transparent"
                    border.width: appButton.visualFocus ? 1 : 0
                    border.color: Appearance.text
                }
                contentItem: Item {
                    TrayIcon {
                        anchors.centerIn: parent
                        width: 17
                        height: 17
                        entry: appButton.modelData
                    }
                }
                onClicked: {
                    if (modelData.itemIsMenu)
                        root.menu(modelData, appButton);
                    else
                        TrayModel.primary(modelData, mapToGlobal(width / 2, height / 2));
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: root.menu(appButton.modelData, appButton)
                }
                TapHandler {
                    acceptedButtons: Qt.MiddleButton
                    onTapped: TrayModel.secondary(appButton.modelData, appButton.mapToGlobal(appButton.width / 2, appButton.height / 2))
                }
                WheelHandler {
                    onWheel: event => {
                        if (!appButton.modelData.item)
                            return;
                        if (event.angleDelta.y)
                            appButton.modelData.item.scroll(event.angleDelta.y, "vertical");
                        if (event.angleDelta.x)
                            appButton.modelData.item.scroll(event.angleDelta.x, "horizontal");
                        event.accepted = true;
                    }
                }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Menu || event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier) {
                        root.menu(modelData, appButton);
                        event.accepted = true;
                    }
                }
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: TrayModel.title(modelData) + "\n" + TrayModel.status(modelData)
            }
        }
        TrayButton {
            id: overflow
            objectName: "trayOverflow"
            width: 29
            height: 32
            iconName: Appearance.bottom !== root.expanded ? "go-up-symbolic" : "go-down-symbolic"
            label: qsTr("Show background apps")
            highlighted: root.expanded
            onClicked: {
                root.requestedMenuKey = "";
                root.requestAnchor = overflow;
                root.activated();
            }
        }
    }
}
