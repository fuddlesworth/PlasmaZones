// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Bluetooth, an adapter power state indicator.
//
// Self-contained: owns a BluetoothHost and follows the first adapter's
// powered state. Hides entirely when the machine has no Bluetooth adapter.
// Paired-device detail and connect/disconnect live in BluetoothPanel,
// which the chip's `activated` opens and which binds its own host.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.Bluetooth

BarWidget {
    id: root

    /// Relayed by BarController as BarRegistry.widgetActivated("bluetooth").
    /// See Network.qml for why this is declared per widget rather than on
    /// BarWidget.
    signal activated

    BluetoothHost {
        id: host
    }

    readonly property BluetoothAdapter adapter: host.adapterCount > 0 ? host.adapterAt(0) : null
    readonly property bool powered: root.adapter ? root.adapter.powered : false

    // No adapter means no Bluetooth hardware: hide rather than show a
    // perpetually-off glyph.
    // Named rather than repeated: this value feeds the collapse
    // arithmetic as well as the glyph, so a bare literal in both places
    // could silently desync them. Matches Tray's local convention.
    readonly property int iconSize: 18

    available: host.adapterCount > 0
    contentWidth: root.iconSize
    contentHeight: root.iconSize

    Accessible.role: Accessible.Indicator
    Accessible.name: root.powered ? qsTr("Bluetooth on") : qsTr("Bluetooth off")

    Kirigami.Icon {
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        // breeze ships no bare bluetooth-* names; these are the ones it
        // actually carries, so both states draw a real glyph rather than the
        // unknown-icon fallback.
        source: root.powered ? "network-bluetooth-activated" : "network-bluetooth"
        isMask: true
        color: root.powered ? Theme.primary : Theme.on_surface_variant
        scale: trigger.pressed ? 0.94 : 1
        opacity: trigger.hovered ? 1 : 0.85

        Behavior on scale {
            NumberAnimation {
                duration: Motion.duration_tick
                easing: Motion.reveal
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: trigger.hovered ? Motion.duration_enter : Motion.duration_release
                easing: trigger.hovered ? Motion.enter : Motion.release
            }
        }
    }

    ChipTrigger {
        id: trigger

        actionName: qsTr("Show Bluetooth panel")
        // An adapter is what the panel acts on: powering the radio, listing
        // devices, starting discovery all go through one. With none there is
        // nothing to show, and the chip is hidden anyway.
        active: host.adapterCount > 0
        onTriggered: root.activated()
    }
}
