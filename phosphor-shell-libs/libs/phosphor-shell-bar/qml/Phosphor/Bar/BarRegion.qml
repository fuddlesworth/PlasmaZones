// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    property alias groups: slot.groups
    property alias registry: slot.registry
    property alias screenWidth: slot.screenWidth
    property alias screenName: slot.screenName
    readonly property int mountedCount: slot.mountedCount
    readonly property Item hoveredCell: slot.hoveredCell
    property real maximumWidth: 10000
    readonly property bool overflowing: slot.implicitWidth > maximumWidth
    readonly property real scrollOffset: viewport.contentX
    function cellFor(id): Item {
        return slot.cellFor(id);
    }
    implicitWidth: Math.max(0, Math.min(slot.implicitWidth, maximumWidth))
    implicitHeight: Math.max(slot.implicitHeight, 30)
    Flickable {
        id: viewport
        x: root.overflowing ? 20 : 0
        width: Math.max(0, root.width - x * 2)
        height: root.height
        contentWidth: slot.implicitWidth
        contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        onContentWidthChanged: contentX = Math.max(0, Math.min(contentX, contentWidth - width))
        onWidthChanged: contentX = Math.max(0, Math.min(contentX, contentWidth - width))
        Slot {
            id: slot
            maximumWidth: root.maximumWidth
            y: (viewport.height - height) / 2
        }
    }
    Repeater {
        model: root.overflowing ? 2 : 0
        ShellButton {
            required property int index
            x: index === 0 ? 0 : root.width - width
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 28
            leftPadding: 0
            rightPadding: 0
            text: index === 0 ? "‹" : "›"
            flat: true
            enabled: index === 0 ? viewport.contentX > 0 : viewport.contentX < viewport.contentWidth - viewport.width
            Accessible.name: index === 0 ? qsTr("Previous bar widgets") : qsTr("Next bar widgets")
            onClicked: viewport.contentX = Math.max(0, Math.min(viewport.contentWidth - viewport.width, viewport.contentX + (index === 0 ? -1 : 1) * Math.max(100, viewport.width * 0.7)))
        }
    }
}
