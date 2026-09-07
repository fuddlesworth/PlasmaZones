// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.BatteryPanel, the battery chip's panel.
//
// The aggregate display device at the top with the time remaining, then
// every other power-supply UPower knows about: a second battery, a mouse,
// a headset. Read-only. UPower reports; it does not take instructions, and
// power PROFILES are a different daemon (power-profiles-daemon) this panel
// deliberately does not reach into.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.UPower

PanelFrame {
    id: root

    title: qsTr("Battery")
    iconName: root._display && root._display.iconName ? root._display.iconName : "battery"
    subtitle: root._display ? root._stateLine(root._display) : qsTr("No battery")

    UPowerHost {
        id: upower
    }

    readonly property UPowerDevice _display: upower.displayDevice

    /// The state sentence for a device: what it is doing and, when UPower
    /// has an estimate, how long that has left to run.
    ///
    /// UPower reports 0 for an estimate it does not have yet (a battery
    /// whose rate has not settled since the cable moved), so a zero is
    /// treated as "unknown" and the sentence simply stops after the state
    /// rather than promising "0 minutes remaining".
    function _stateLine(device) {
        if (!device)
            return "";
        if (device.state === UPowerDevice.Charging) {
            const toFull = device.timeToFull;
            return toFull > 0 ? qsTr("Charging, %1 until full").arg(root._duration(toFull)) : qsTr("Charging");
        }
        if (device.state === UPowerDevice.FullyCharged)
            return qsTr("Fully charged");
        if (device.state === UPowerDevice.Discharging) {
            const toEmpty = device.timeToEmpty;
            return toEmpty > 0 ? qsTr("%1 remaining").arg(root._duration(toEmpty)) : qsTr("On battery");
        }
        return qsTr("On mains power");
    }

    /// Seconds as a short human duration. Hours and minutes only: UPower's
    /// estimate is not accurate to the second and printing one would imply
    /// a precision it does not have.
    function _duration(seconds) {
        const total = Math.round(seconds / 60);
        const hours = Math.floor(total / 60);
        const minutes = total % 60;
        if (hours <= 0)
            return qsTr("%1 min").arg(minutes);
        return qsTr("%1 h %2 min").arg(hours).arg(minutes);
    }

    Item {
        width: parent.width
        implicitHeight: charge.implicitHeight + Tokens.spacing_s

        Column {
            id: charge

            width: parent.width
            spacing: Tokens.spacing_xs

            TabularText {
                text: root._display ? Math.round(root._display.percentage) + "%" : "—"
                color: root._display && root._display.state === UPowerDevice.Charging ? Theme.success : Theme.on_surface
                font.pixelSize: Tokens.font_size_display_s
            }

            // The charge bar. A plain rectangle pair rather than a slider:
            // this value is reported, never set, and a control affordance
            // on it would invite a drag that does nothing.
            Rectangle {
                width: parent.width
                height: 4
                radius: Tokens.radius_full
                color: Theme.surface_variant

                Rectangle {
                    width: parent.width * (root._display ? Math.max(0, Math.min(1, root._display.percentage / 100)) : 0)
                    height: parent.height
                    radius: parent.radius
                    color: root._display && root._display.state === UPowerDevice.Charging ? Theme.success : Theme.primary

                    Behavior on width {
                        NumberAnimation {
                            duration: Motion.duration_short_4
                            easing: Motion.standard
                        }
                    }
                }
            }
        }
    }

    PanelRow {
        width: parent.width
        visible: root._display !== null && root._display.healthPercentage > 0
        pressable: false
        iconName: "battery"
        label: qsTr("Health")
        // Design capacity against present capacity: how much of the
        // battery's original charge it can still hold.
        trailingText: root._display ? Math.round(root._display.healthPercentage) + "%" : ""
    }

    PanelRow {
        width: parent.width
        visible: root._display !== null && root._display.energyRate > 0
        pressable: false
        iconName: "battery-caution"
        label: qsTr("Draw")
        trailingText: root._display ? root._display.energyRate.toFixed(1) + " W" : ""
    }

    Text {
        width: parent.width
        visible: peripherals.count > 0
        text: qsTr("Other devices")
        color: Theme.on_surface_variant
        font.pixelSize: Tokens.font_size_label_s
        font.family: Tokens.font_family_ui
        topPadding: Tokens.spacing_s
    }

    UPowerDeviceModel {
        id: peripherals

        host: upower
    }

    Repeater {
        model: peripherals

        delegate: PanelRow {
            id: deviceRow

            // Only the object is required from the model, and every field
            // below reads off it. Taking the model's `iconName` role as a
            // required property would collide with PanelRow's own property
            // of that name — the delegate cannot both receive and declare
            // it.
            required property var device

            width: parent ? parent.width : 0
            // The laptop's own battery is already the headline above, so
            // showing it again in the list would be the same number twice.
            visible: deviceRow.device !== null && !deviceRow.device.isLaptopBattery && deviceRow.device.isPresent
            pressable: false
            iconName: deviceRow.device && deviceRow.device.iconName !== "" ? deviceRow.device.iconName : "battery"
            label: deviceRow.device && deviceRow.device.model !== "" ? deviceRow.device.model : qsTr("Unnamed device")
            trailingText: deviceRow.device ? Math.round(deviceRow.device.percentage) + "%" : ""
        }
    }
}
