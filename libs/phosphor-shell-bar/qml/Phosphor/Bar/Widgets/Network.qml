// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Network, a connectivity state indicator.
//
// Self-contained: owns a NetworkHost and shows a coarse connected /
// offline glyph from networkingEnabled + wirelessEnabled. Per-device
// signal strength and SSID (NetworkDevice / AccessPoint) are a follow-up;
// this first cut is a robust state glyph, not a full applet. The detailed
// network panel binds the same host when the control center tile lands.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.Network

Item {
    id: root

    NetworkHost {
        id: host
    }

    readonly property bool online: host.networkingEnabled
    readonly property string iconName: !root.online ? "network-offline" : host.wirelessEnabled ? "network-wireless" : "network-wired"

    implicitWidth: 18
    implicitHeight: 18

    Accessible.role: Accessible.Indicator
    Accessible.name: root.online ? "Network connected" : "Network offline"

    Kirigami.Icon {
        anchors.centerIn: parent
        width: 18
        height: 18
        source: root.iconName
        isMask: true
        color: root.online ? Theme.on_surface : Theme.on_surface_variant
    }
}
