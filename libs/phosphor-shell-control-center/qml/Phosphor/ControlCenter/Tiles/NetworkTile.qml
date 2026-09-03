// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.NetworkTile, the Wi-Fi toggle.
//
// Owns a NetworkHost and toggles the wireless radio. The bar's Network
// widget noted that "the detailed network panel binds the same host when
// the control center tile lands" — this is that tile, and it deliberately
// owns its own host rather than sharing the bar's: NetworkHost is a thin
// view over NetworkManager's D-Bus state, and two of them cost one extra
// set of property mirrors, not two connections' worth of traffic.
//
// The toggle governs the WIRELESS RADIO specifically, not NetworkManager's
// global networking switch. Flipping the global switch from a tile labelled
// Wi-Fi would take an ethernet link down with it.

import QtQuick
import Phosphor.ControlCenter
import Phosphor.Service.Network

Tile {
    id: root

    NetworkHost {
        id: host
    }

    // Reachability, not the radio switch: a machine with the radio on and
    // no association is not connected. Same reading as the bar widget,
    // including UnknownConnectivity counting as online when something is
    // actually carrying traffic (distros that ship connectivity checking
    // disabled report it forever, and it is also the pre-first-check
    // value).
    readonly property bool _online: host.connectivity === NetworkHost.Full || (host.connectivity === NetworkHost.UnknownConnectivity && host.primaryConnectionType.length > 0)
    readonly property bool _limited: host.connectivity === NetworkHost.Portal || host.connectivity === NetworkHost.Limited
    readonly property bool _wireless: host.primaryConnectionType.includes("wireless")

    iconName: {
        if (!host.wirelessEnabled)
            return "network-wireless-disconnected";
        if (root._limited)
            return "network-wireless-acquiring";
        return root._online && root._wireless ? "network-wireless" : "network-wireless-disconnected";
    }
    label: qsTr("Wi-Fi")
    sublabel: {
        if (!host.wirelessEnabled)
            return qsTr("Off");
        if (root._limited)
            return qsTr("Limited");
        if (root._online && root._wireless)
            return qsTr("Connected");
        // Radio on, nothing associated. Distinct from Off so the toggle's
        // state and the connection's state stay separable.
        return qsTr("Not connected");
    }
    // The radio switch, which is what this tile writes.
    active: host.wirelessEnabled
    // Networking disabled wholesale (airplane mode, or NM stopped) leaves
    // no radio to turn on, so the tile goes inert rather than offering a
    // toggle whose write NetworkManager will refuse.
    available: host.networkingEnabled
    hasDetail: true

    onToggled: host.wirelessEnabled = !host.wirelessEnabled
}
