// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import Phosphor.Theme

AbstractButton {
    id: root
    property var windowInfo: ({})
    property bool row: false
    property bool selected: false
    property bool draggable: false
    property int colorIndex: 0
    readonly property bool horizontal: row || height < 92
    readonly property color hue: Appearance.windowColor(colorIndex)
    readonly property string appName: {
        if (windowInfo.occupied === false && !windowInfo.windowId)
            return qsTr("Empty zone %1").arg(windowInfo.zoneNumber || "");
        const id = String(windowInfo.appId || windowInfo.label || "").replace(/\.desktop$/, "").split(".").pop();
        return id ? id.charAt(0).toUpperCase() + id.slice(1).replace(/[-_]/g, " ") : qsTr("Window");
    }
    readonly property string appIcon: {
        const name = appName.toLowerCase();
        return name === "kate" || name === "konsole" ? "utilities-terminal" : name.indexOf("firefox") >= 0 ? "internet-web-browser" : name === "dolphin" ? "folder" : windowInfo.appId || "application-x-executable";
    }
    signal dragFinished(real localX, real localY)
    implicitHeight: 56
    implicitWidth: 200
    padding: 10
    Accessible.name: appName + ", " + (windowInfo.title || "")
    ToolTip.visible: hovered && (windowInfo.title || "") !== ""
    ToolTip.text: windowInfo.title || ""
    ToolTip.delay: 700
    background: Rectangle {
        radius: root.row ? 8 : Appearance.radius * 0.45
        color: root.row ? (root.selected || root.hovered ? Appearance.card : "transparent") : Qt.tint(Appearance.recess, Qt.alpha(root.hue, root.selected ? 0.22 : root.hovered ? 0.2 : 0.12))
        border.width: root.row ? (root.visualFocus ? 1 : 0) : 1
        border.color: root.selected || root.visualFocus ? Appearance.text : Qt.alpha(root.hue, 0.44)
        Rectangle {
            visible: !root.row && root.selected
            x: parent.radius
            width: parent.width - x * 2
            height: 2
            color: Appearance.text
        }
    }
    contentItem: Item {
        clip: true
        ShellIcon {
            id: icon
            x: 0
            y: root.horizontal ? (parent.height - height) / 2 : Math.max(0, (parent.height - labels.height - height - 9) / 2)
            width: root.horizontal ? 19 : 24
            height: width
            source: root.appIcon
            color: root.hue
        }
        Column {
            id: labels
            x: root.horizontal ? 30 : 0
            y: root.horizontal ? (parent.height - height) / 2 : icon.y + icon.height + 9
            width: parent.width - x
            spacing: root.horizontal ? 1 : 7
            Text {
                width: parent.width
                text: root.appName
                font.family: Tokens.font_family_ui
                font.pixelSize: 11
                color: Appearance.text
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.windowInfo.title || (root.windowInfo.occupied === false ? qsTr("Place selected window") : root.windowInfo.minimized ? qsTr("Minimized") : qsTr("Untitled window"))
                font.family: Tokens.font_family_ui
                font.pixelSize: 9
                color: Appearance.muted
                elide: Text.ElideRight
                maximumLineCount: root.row ? 1 : 2
                wrapMode: root.row ? Text.NoWrap : Text.WrapAnywhere
            }
        }
        Text {
            visible: root.row && (root.windowInfo.urgent || root.windowInfo.offscreen)
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.windowInfo.urgent ? "●" : "↗"
            color: root.hue
            font.pixelSize: 10
        }
    }
    DragHandler {
        id: drag
        enabled: root.draggable
        target: null
        property point lastPosition: Qt.point(0, 0)
        onCentroidChanged: if (active)
            lastPosition = centroid.position
        onActiveChanged: if (!active)
            root.dragFinished(lastPosition.x, lastPosition.y)
    }
}
