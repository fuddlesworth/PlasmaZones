// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

QuickDetailFrame {
    id: root
    required property var controller
    signal panelRequested(string panelId)
    title: i18n("Power profiles")
    footerText: i18n("Device power management")
    footerIcon: "speedometer"
    readonly property var profiles: ({
            "power-saver": {
                title: i18n("Power saver"),
                description: i18n("Use less energy and extend battery life."),
                icon: "battery-low"
            },
            "balanced": {
                title: i18n("Balanced"),
                description: i18n("Adjust performance to what you are doing."),
                icon: "speedometer"
            },
            "performance": {
                title: i18n("Performance"),
                description: i18n("Favor speed with higher power use."),
                icon: "power-profile-performance"
            }
        })
    readonly property var supportedProfiles: controller.powerProfiles.filter(id => root.profiles[id] !== undefined)
    readonly property string profileTitle: profiles[controller.powerProfile]?.title ?? i18n("Unknown profile")

    DetailNotice {
        text: root.controller.powerError
        error: true
    }
    DetailEmptyState {
        visible: !root.controller.powerAvailable
        title: i18n("Power profiles are unavailable")
        description: i18n("Reconnect to the power service to change the active profile.")
        iconName: "speedometer"
        actionText: i18n("Try again")
        onActivated: root.controller.refreshPower()
    }
    Column {
        visible: root.controller.powerAvailable
        width: parent.width
        spacing: Tokens.spacing_l
        DetailCard {
            title: root.profileTitle
            description: i18n("Choose how your device balances speed and battery life.")
            iconName: "speedometer"
            status: root.controller.powerPending ? i18n("Changing profile") : i18n("Active profile")
        }
        DetailSectionHeading {
            title: i18n("Power profile")
        }
        Column {
            width: parent.width
            spacing: Tokens.spacing_s
            Repeater {
                id: choices
                model: root.supportedProfiles
                delegate: DetailDeviceRow {
                    required property string modelData
                    required property int index
                    objectName: "powerProfile-" + modelData
                    title: root.profiles[modelData].title
                    subtitle: root.profiles[modelData].description
                    iconName: root.profiles[modelData].icon
                    radio: true
                    selected: root.controller.powerProfile === modelData
                    enabled: !root.controller.powerPending
                    onClicked: root.controller.setPowerProfile(modelData)
                    Keys.onUpPressed: event => {
                        if (index > 0)
                            choices.itemAt(index - 1).forceActiveFocus();
                        event.accepted = true;
                    }
                    Keys.onDownPressed: event => {
                        if (index + 1 < choices.count)
                            choices.itemAt(index + 1).forceActiveFocus();
                        event.accepted = true;
                    }
                }
            }
        }
        DetailNotice {
            text: !root.controller.performanceDegraded ? "" : root.controller.performanceDegraded === "high-operating-temperature" ? i18n("Performance is reduced while the device is warm. You can still select the Performance profile.") : root.controller.performanceDegraded === "lap-detected" ? i18n("Performance is reduced while the device is on your lap. You can still select the Performance profile.") : i18n("The device is limiting performance. You can still select the Performance profile.")
        }
        DetailText {
            width: parent.width
            visible: root.supportedProfiles.indexOf("performance") < 0
            text: i18n("Only profiles supported by this device are shown.")
            font.pixelSize: Tokens.font_size_label_s
            muted: true
        }
        ShellButton {
            objectName: "powerBatteryDetails"
            text: i18n("Battery details")
            iconName: "battery"
            onClicked: root.panelRequested("battery")
        }
    }
}
