// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

QuickDetailFrame {
    id: root
    required property var controller
    title: i18n("Airplane mode")
    footerText: i18n("Wired connections stay connected")
    footerIcon: "network-wired"
    headerAction: Component {
        DetailSwitch {
            objectName: "airplaneToggle"
            on: root.controller.enabled
            enabled: root.controller.available && !root.controller.pending
            Accessible.name: i18n("Airplane mode")
            onClicked: root.controller.setEnabled(!on)
        }
    }

    DetailNotice {
        text: root.controller.error
        error: true
    }
    ShellButton {
        objectName: "airplaneRestore"
        visible: root.controller.restoreAvailable
        text: i18n("Restore previous radio settings")
        enabled: !root.controller.pending
        onClicked: root.controller.restoreRadios()
    }
    DetailEmptyState {
        visible: !root.controller.available
        title: i18n("Wireless controls are unavailable")
        description: i18n("Reconnect to the radio services to change airplane mode.")
        iconName: "flightmode-on"
        actionText: i18n("Try again")
        onActivated: root.controller.refresh()
    }
    Column {
        visible: root.controller.available
        width: parent.width
        spacing: Tokens.spacing_l
        DetailCard {
            title: root.controller.enabled ? i18n("A little time offline.") : i18n("Stay connected.")
            description: root.controller.enabled ? i18n("Wi-Fi and mobile broadband are off. You can turn Bluetooth back on for headphones and other devices.") : i18n("Turn off Wi-Fi, Bluetooth and mobile broadband together. Turning airplane mode off restores their previous settings.")
            iconName: "flightmode-on"
            status: root.controller.pending ? i18n("Updating radios") : root.controller.enabled ? i18n("On") : i18n("Off")
        }
        DetailSectionHeading {
            title: i18n("Wireless radios")
        }
        Column {
            width: parent.width
            spacing: Tokens.spacing_s
            DetailDeviceRow {
                objectName: "airplaneWifi"
                title: i18n("Wi-Fi")
                subtitle: !root.controller.wifiAvailable ? i18n("Unavailable") : !root.controller.wifiHardwareEnabled ? i18n("Use the wireless switch on your device to turn Wi-Fi on.") : root.controller.enabled ? i18n("Turning on exits airplane mode.") : root.controller.wifiEnabled ? i18n("On") : i18n("Off")
                iconName: "network-wireless"
                selected: root.controller.wifiEnabled
                radio: true
                Accessible.role: Accessible.CheckBox
                enabled: root.controller.wifiAvailable && root.controller.wifiHardwareEnabled && !root.controller.pending
                onClicked: root.controller.setWifiEnabled(!root.controller.wifiEnabled)
            }
            DetailDeviceRow {
                objectName: "airplaneBluetooth"
                title: i18n("Bluetooth")
                subtitle: !root.controller.bluetoothAvailable ? i18n("Unavailable") : root.controller.bluetoothEnabled ? i18n("On") : i18n("Off")
                iconName: "preferences-system-bluetooth"
                selected: root.controller.bluetoothEnabled
                radio: true
                Accessible.role: Accessible.CheckBox
                enabled: root.controller.bluetoothAvailable && !root.controller.pending
                onClicked: root.controller.setBluetoothEnabled(!root.controller.bluetoothEnabled)
            }
            DetailDeviceRow {
                objectName: "airplaneMobileBroadband"
                visible: root.controller.wwanAvailable
                title: i18n("Mobile broadband")
                subtitle: !root.controller.wwanHardwareEnabled ? i18n("Use the wireless switch on your device to turn mobile broadband on.") : root.controller.enabled ? i18n("Turning on exits airplane mode.") : root.controller.wwanEnabled ? i18n("On") : i18n("Off")
                iconName: "network-mobile"
                selected: root.controller.wwanEnabled
                radio: true
                Accessible.role: Accessible.CheckBox
                enabled: root.controller.wwanHardwareEnabled && !root.controller.pending
                onClicked: root.controller.setWwanEnabled(!root.controller.wwanEnabled)
            }
        }
        DetailNotice {
            text: root.controller.enabled && root.controller.bluetoothEnabled ? i18n("Bluetooth is on while airplane mode remains enabled.") : ""
        }
    }
}
