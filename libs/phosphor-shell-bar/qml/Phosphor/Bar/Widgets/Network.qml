// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Network, a connectivity state indicator.
//
// Self-contained: owns a NetworkHost and shows a coarse glyph derived from
// NetworkManager's connectivity state plus the type of the connection
// actually carrying traffic. Per-device signal strength and SSID
// (NetworkDevice / AccessPoint) are a follow-up; this first cut is a robust
// state glyph, not a full applet. The detailed network panel binds the same
// host when the control center tile lands.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.Network

BarWidget {
    id: root

    NetworkHost {
        id: host
    }

    // Reachability, not NetworkManager's global networking switch: an
    // unplugged box with networking enabled is offline, and the switch says
    // nothing about that.
    //
    // UnknownConnectivity has to count as online when something is actually
    // connected. NetworkManager reports it whenever connectivity checking is
    // switched off (several distros ship it that way, and privacy-minded
    // users disable the check URI) and it is also the value cached before
    // the first check completes, so keying purely on Full would leave a
    // perfectly working machine reading "offline" forever.
    readonly property bool online: host.connectivity === NetworkHost.Full || (host.connectivity === NetworkHost.UnknownConnectivity && host.primaryConnectionType.length > 0)
    // Reachable but degraded: behind a captive portal, or no route to the
    // wider internet.
    readonly property bool limited: host.connectivity === NetworkHost.Portal || host.connectivity === NetworkHost.Limited

    // The glyph follows the connection actually carrying traffic, not the
    // wifi radio switch (a laptop on ethernet with wifi merely enabled is
    // wired). These are NetworkManager connection types verbatim:
    // "802-11-wireless" for wifi, "802-3-ethernet" for ethernet, and
    // "gsm" / "cdma" / "wwan" for mobile broadband.
    //
    // Wired is an explicit test rather than the fallback, because a VPN or
    // WireGuard tunnel holding the default route reports its own type and
    // must not be drawn as a link this widget knows nothing about.
    readonly property string _type: host.primaryConnectionType
    readonly property bool wireless: root._type.includes("wireless")
    readonly property bool cellular: root._type.includes("gsm") || root._type.includes("cdma")
    // Ethernet plus NM's virtual wired types: a default route sitting on a
    // libvirt/docker/NM bridge is still a wired link, and is common enough
    // that leaving it to the unclassified fallback would be wrong more often
    // than right.
    readonly property bool wired: ["ethernet", "bridge", "bond", "team", "vlan"].some(t => root._type.includes(t))

    readonly property string iconName: {
        if (!root.online && !root.limited)
            return "network-offline";
        if (root.cellular)
            // "network-modem" is the generic freedesktop name a theme
            // actually ships; breeze has only the parameterised
            // network-mobile-<level> family, so that name resolves to
            // nothing and falls back to the unknown-icon glyph.
            return "network-modem";
        if (root.wireless)
            return "network-wireless";
        if (root.wired)
            return "network-wired";
        // Connected over something this widget does not classify (a VPN or
        // WireGuard tunnel holding the default route, bluetooth PAN, ...).
        return "network-connect";
    }

    // No NetworkManager on the bus (systemd-networkd, iwd, connman, or NM
    // simply not up) leaves the host inert with UnknownConnectivity and an
    // empty type, which would otherwise paint a permanent, unrecoverable
    // "offline" glyph on a perfectly connected machine. Gate on hardware the
    // way Bluetooth and Battery do.
    // Named rather than repeated: this value feeds the collapse
    // arithmetic as well as the glyph, so a bare literal in both places
    // could silently desync them. Matches Tray's local convention.
    readonly property int iconSize: 18

    available: host.deviceCount > 0
    contentWidth: root.iconSize
    contentHeight: root.iconSize

    Accessible.role: Accessible.Indicator
    Accessible.name: root.online ? "Network connected" : root.limited ? "Network limited" : "Network offline"

    Kirigami.Icon {
        anchors.centerIn: parent
        width: 18
        height: 18
        source: root.iconName
        isMask: true
        color: root.online ? Theme.on_surface : root.limited ? Theme.warning : Theme.on_surface_variant
    }
}
