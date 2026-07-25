// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Tray, the StatusNotifierItem system tray.
//
// Self-contained: owns a StatusNotifierHost (the DBus watcher + item
// collection) and its model, and renders one clickable icon per item.
// Button mapping mirrors KDE Plasma: left = Activate, middle =
// SecondaryActivate, right / menu-items = ContextMenu. Scroll forwards as
// SNI Scroll (volume/brightness applets). The dbusmenu cascade popup is a
// later popout surface; right-click falls back to the item's own
// ContextMenu() until it lands. The whole widget collapses to zero width
// when the tray is empty so the slot closes the gap.

import QtQuick
import Phosphor.Theme
import Phosphor.Service.Sni

Item {
    id: root

    readonly property int delegateSize: 24
    readonly property int iconSize: 18
    readonly property real passiveOpacity: 0.5

    implicitWidth: trayModel.count > 0 ? trayRow.implicitWidth : 0
    implicitHeight: delegateSize
    visible: trayModel.count > 0

    StatusNotifierHost {
        id: trayHost
    }

    StatusNotifierItemModel {
        id: trayModel

        host: trayHost
    }

    Row {
        id: trayRow

        anchors.verticalCenter: parent.verticalCenter
        spacing: Tokens.spacing_xs

        Repeater {
            model: trayModel

            delegate: Rectangle {
                id: trayDelegate

                required property int index
                required property string itemId
                required property string title
                required property string iconUrl
                required property var status
                required property string toolTipTitle
                required property string toolTipBody
                required property string menuPath
                required property bool itemIsMenu
                required property string dbusService

                width: root.delegateSize
                height: root.delegateSize
                radius: Tokens.radius_s
                color: trayMouse.containsMouse ? Theme.surface_container_high : "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: Motion.duration_short_2
                        easing: Motion.standard
                    }
                }

                // Fallback glyph (first letter of the title) when the item
                // ships an icon name the resolver couldn't match, so the
                // slot stays visible and clickable.
                Rectangle {
                    anchors.centerIn: parent
                    visible: trayDelegate.iconUrl.length === 0
                    width: root.iconSize
                    height: root.iconSize
                    radius: width / 2
                    color: Theme.surface_container_high

                    Text {
                        anchors.centerIn: parent
                        text: trayDelegate.title.length > 0 ? trayDelegate.title.charAt(0).toUpperCase() : "?"
                        color: Theme.on_surface
                        font.pixelSize: Tokens.font_size_label_s
                        font.weight: Tokens.font_weight_bold
                        font.family: Tokens.font_family
                    }
                }

                Image {
                    anchors.centerIn: parent
                    visible: trayDelegate.iconUrl.length > 0
                    width: root.iconSize
                    height: root.iconSize
                    // The model's URL-form role encodes a cache key so the
                    // URL changes when the icon data updates; the engine
                    // routes it back through the icon-theme image provider.
                    source: trayDelegate.iconUrl
                    sourceSize.width: root.iconSize * 2
                    sourceSize.height: root.iconSize * 2
                    smooth: true
                    // Dim Passive items so a chatty app that never goes
                    // Active doesn't dominate the bar.
                    opacity: trayDelegate.status === StatusNotifierItem.Passive ? root.passiveOpacity : 1
                }

                MouseArea {
                    id: trayMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton

                    Accessible.role: Accessible.Button
                    Accessible.name: trayDelegate.toolTipTitle.length > 0 ? trayDelegate.toolTipTitle : trayDelegate.title

                    onClicked: mouse => {
                        // Translate to screen coords so the item's process
                        // can place any popup relative to the click.
                        const global = trayDelegate.mapToGlobal(mouse.x, mouse.y);
                        if (mouse.button === Qt.LeftButton) {
                            if (trayDelegate.itemIsMenu)
                                trayModel.contextMenu(trayDelegate.index, global.x, global.y);
                            else
                                trayModel.activate(trayDelegate.index, global.x, global.y);
                        } else if (mouse.button === Qt.MiddleButton) {
                            trayModel.secondaryActivate(trayDelegate.index, global.x, global.y);
                        } else if (mouse.button === Qt.RightButton) {
                            trayModel.contextMenu(trayDelegate.index, global.x, global.y);
                        }
                    }
                    onWheel: wheel => {
                        if (wheel.angleDelta.y !== 0)
                            trayModel.scroll(trayDelegate.index, wheel.angleDelta.y, "vertical");
                        if (wheel.angleDelta.x !== 0)
                            trayModel.scroll(trayDelegate.index, wheel.angleDelta.x, "horizontal");
                    }
                }
            }
        }
    }
}
