// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Phosphor.Bar
import Phosphor.ControlCenter
import Phosphor.Service.Mpris
import Phosphor.Service.UPower

ControlCenter {
    id: root
    provider: ControlCenterRegistry
    tileIds: ControlCenterRegistry.tileIds.filter(id => id !== "idle")
    detailPanels: ({
            "network": networkPanel
        })
    Component {
        id: networkPanel
        NetworkPanel {}
    }
    MprisHost {
        id: media
    }
    UPowerHost {
        id: power
    }
    mediaPlayer: media.playerCount > 0 ? media.playerAt(0) : null
    focusEnabled: NotificationRegistry.doNotDisturb
    focusAvailable: NotificationRegistry.serverActive
    nightLightEnabled: QuickSettings.nightLightEnabled
    nightLightAvailable: QuickSettings.nightLightAvailable
    batterySummary: power.displayDevice && power.displayDevice.isPresent ? Math.round(power.displayDevice.percentage) + "%" : ""
    powerSummary: QuickSettings.powerProfile === "balanced" ? qsTr("Balanced power") : QuickSettings.powerProfile === "power-saver" ? qsTr("Power saver") : QuickSettings.powerProfile === "performance" ? qsTr("Performance") : ""
    notificationSummary: NotificationRegistry.unreadCount ? qsTr("%1 unread notifications").arg(NotificationRegistry.unreadCount) : qsTr("No pending notifications")
    onFocusToggled: NotificationRegistry.doNotDisturb = !NotificationRegistry.doNotDisturb
    onNightLightToggled: QuickSettings.toggleNightLight()
}
