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

    // Named like the sibling widgets, and read by BOTH bindings: `visible`
    // is EFFECTIVE visibility, so sizing off it would close a loop with the
    // slot's cell gate.
    readonly property bool available: trayModel.count > 0

    implicitWidth: root.available ? trayRow.implicitWidth : 0
    implicitHeight: root.delegateSize
    visible: root.available

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
                required property string title
                required property string iconUrl
                // int, not var: the role carries a StatusNotifierItem::Status
                // Q_ENUM value, compared against StatusNotifierItem.Passive.
                required property int status
                required property string toolTipTitle
                required property bool itemIsMenu

                // Primary activation, shared by the left button and the
                // accessibility press action. Items that ARE a menu open
                // their menu instead, mirroring Plasma.
                function primaryActivate(px, py) {
                    const global = trayDelegate.mapToGlobal(px, py);
                    if (trayDelegate.itemIsMenu)
                        trayModel.contextMenu(trayDelegate.index, global.x, global.y);
                    else
                        trayModel.activate(trayDelegate.index, global.x, global.y);
                }

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
                        // The delegate's MouseArea already carries the
                        // accessible name; without this the bare letter is
                        // announced alongside it as its own StaticText node.
                        Accessible.ignored: true
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

                    // Pointer + assistive tech only, no keyboard leg: this widget is an
                    // indicator that happens to be clickable, and the bar's panel takes
                    // no keyboard focus, so there is nothing to Tab from. BarIconButton
                    // carries the full quad because it is the shared button atom and is
                    // meant to work wherever a focused surface hosts it.
                    Accessible.role: Accessible.Button
                    Accessible.name: trayDelegate.toolTipTitle.length > 0 ? trayDelegate.toolTipTitle : trayDelegate.title
                    // Assistive tech activates the item through the same
                    // primary dispatch the left button uses, centred on the
                    // icon since there is no pointer position to map.
                    Accessible.onPressAction: trayDelegate.primaryActivate(trayMouse.width / 2, trayMouse.height / 2)

                    onClicked: mouse => {
                        if (mouse.button === Qt.LeftButton) {
                            // primaryActivate does its own mapping.
                            trayDelegate.primaryActivate(mouse.x, mouse.y);
                            return;
                        }
                        // Translate to screen coords so the item's process
                        // can place any popup relative to the click.
                        const global = trayDelegate.mapToGlobal(mouse.x, mouse.y);
                        if (mouse.button === Qt.MiddleButton)
                            trayModel.secondaryActivate(trayDelegate.index, global.x, global.y);
                        else if (mouse.button === Qt.RightButton)
                            trayModel.contextMenu(trayDelegate.index, global.x, global.y);
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
