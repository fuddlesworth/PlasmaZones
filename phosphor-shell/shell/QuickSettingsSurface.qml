// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Bar
import Phosphor.ControlCenter
import Phosphor.Service.Mpris
import Phosphor.Service.UPower

ControlCenter {
    id: root
    property string initialPanel: ""
    Component.onCompleted: {
        if (initialPanel)
            root.openPanel(initialPanel);
    }
    readonly property string outputName: root.Window.window && root.Window.window.screen ? root.Window.window.screen.name : ""
    readonly property var wallpaper: AppearanceStore.values.wallpapers[outputName] || AppearanceStore.values.wallpapers[""] || ({})
    function openAppearance(page) {
        PickerRegistry.page = page;
        PickerRegistry.selectedScreen = outputName;
        ControlCenterRegistry.requestPanel("appearance");
    }
    provider: ControlCenterRegistry
    // Popout content is created in the engine root context, outside shell.qml.
    onPanelRequested: panelId => ControlCenterRegistry.requestPanel(panelId)
    tileIds: ControlCenterRegistry.tileIds.filter(id => id !== "idle")
    detailPanels: ({
            "network": networkPanel,
            "bluetooth": bluetoothPanel,
            "audio": audioPanel,
            "microphone": microphonePanel,
            "nightlight": nightLightPanel,
            "power": powerPanel,
            "airplane": airplanePanel,
            "darkmode": darkModePanel,
            "wallpaper": wallpaperPanel,
            "battery": batteryPanel
        })
    Component {
        id: networkPanel
        NetworkPanel {}
    }
    Component {
        id: bluetoothPanel
        BluetoothPanel {}
    }
    Component {
        id: audioPanel
        AudioPanel {}
    }
    Component {
        id: microphonePanel
        AudioPanel {
            tab: "input"
        }
    }
    Component {
        id: nightLightPanel
        NightLightPanel {
            controller: QuickSettings
        }
    }
    Component {
        id: powerPanel
        PowerProfilesPanel {
            controller: QuickSettings
        }
    }
    Component {
        id: airplanePanel
        AirplanePanel {
            controller: Airplane
        }
    }
    Component {
        id: darkModePanel
        DarkModePanel {
            controller: ColorMode
            onAppearanceRequested: page => root.openAppearance(page)
        }
    }
    Component {
        id: wallpaperPanel
        WallpaperPanel {
            screenName: root.outputName
            onAppearanceRequested: page => root.openAppearance(page)
        }
    }
    Component {
        id: batteryPanel
        BatteryPanel {
            controller: QuickSettings
            serviceHost: power
            deviceModel: powerDevices
        }
    }
    MprisHost {
        id: media
    }
    UPowerHost {
        id: power
    }
    UPowerDeviceModel {
        id: powerDevices
        host: power
    }
    mediaPlayer: media.playerCount > 0 ? media.playerAt(0) : null
    focusEnabled: NotificationRegistry.doNotDisturb
    focusAvailable: NotificationRegistry.serverActive
    nightLightEnabled: QuickSettings.nightLightEnabled
    nightLightAvailable: QuickSettings.nightLightAvailable
    nightLightSummary: !QuickSettings.nightLightAvailable ? i18n("Unavailable") : QuickSettings.nightLightPaused || QuickSettings.nightLightInhibited ? i18n("Paused") : !QuickSettings.nightLightEnabled ? i18n("Off") : QuickSettings.nightLightRunning ? i18n("On") : i18n("Scheduled")
    darkModeEnabled: ColorMode.effectiveDark
    darkModeSummary: ColorMode.mode === "system" ? i18n("Follow system") : ColorMode.effectiveDark ? i18n("Dark") : i18n("Light")
    airplaneEnabled: Airplane.enabled
    airplaneSummary: !Airplane.available ? i18n("Unavailable") : Airplane.pending ? i18n("Updating") : Airplane.enabled ? i18n("On") : i18n("Off")
    wallpaperSummary: wallpaper.path ? wallpaper.path.split("/").pop() : i18n("Choose an image")
    batterySummary: power.available && power.displayDevice && power.displayDevice.isPresent && Number.isFinite(power.displayDevice.percentage) ? Math.round(Math.max(0, Math.min(100, power.displayDevice.percentage))) + "%" : i18n("Battery")
    powerSummary: QuickSettings.powerProfile === "balanced" ? i18n("Balanced") : QuickSettings.powerProfile === "power-saver" ? i18n("Power saver") : QuickSettings.powerProfile === "performance" ? i18n("Performance") : i18n("Unavailable")
    notificationSummary: NotificationRegistry.unreadCount ? i18n("%1 unread notifications").arg(NotificationRegistry.unreadCount) : i18n("No pending notifications")
    onFocusToggled: NotificationRegistry.doNotDisturb = !NotificationRegistry.doNotDisturb
    onNightLightToggled: QuickSettings.toggleNightLight()
}
