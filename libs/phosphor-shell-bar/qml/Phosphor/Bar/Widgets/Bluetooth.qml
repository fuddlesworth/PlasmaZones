// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Bluetooth, an adapter power state indicator.
//
// Self-contained: owns a BluetoothHost and follows the first adapter's
// powered state. Hides entirely when the machine has no Bluetooth adapter.
// Paired-device detail and a connect/disconnect menu are a follow-up
// (the control center tile binds the same host); this first cut is a
// robust state glyph.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.Bluetooth

Item {
    id: root

    BluetoothHost {
        id: host
    }

    readonly property BluetoothAdapter adapter: host.adapterCount > 0 ? host.adapterAt(0) : null
    readonly property bool powered: root.adapter ? root.adapter.powered : false

    // No adapter means no Bluetooth hardware: hide rather than show a
    // perpetually-off glyph.
    //
    // Both bindings read this host-derived predicate, never `visible`.
    // Item.visible READS as EFFECTIVE visibility, and the slot gates its
    // cell on this very implicitWidth, so deriving the width from `visible`
    // closes a loop that latches at zero and never recovers.
    readonly property bool available: host.adapterCount > 0

    visible: root.available
    implicitWidth: root.available ? 18 : 0
    implicitHeight: 18

    Accessible.role: Accessible.Indicator
    Accessible.name: root.powered ? "Bluetooth on" : "Bluetooth off"

    Kirigami.Icon {
        anchors.centerIn: parent
        width: 18
        height: 18
        // breeze ships no bare bluetooth-* names; these are the ones it
        // actually carries, so both states draw a real glyph rather than the
        // unknown-icon fallback.
        source: root.powered ? "network-bluetooth-activated" : "network-bluetooth"
        isMask: true
        color: root.powered ? Theme.primary : Theme.on_surface_variant
    }
}
