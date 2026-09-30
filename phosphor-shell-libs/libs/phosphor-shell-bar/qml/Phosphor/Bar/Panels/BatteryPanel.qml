// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import QtQml.Models
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.UPower

QuickDetailFrame {
    id: root
    title: i18n("Battery")
    footerIcon: "battery"
    footerText: i18n("Estimates change with your workload.")
    property var controller: null
    property var serviceHost: null
    property var deviceModel: devices
    signal panelRequested(string panel)
    readonly property var resolvedPowerService: serviceHost || localService.object
    readonly property bool serviceAvailable: !!resolvedPowerService && resolvedPowerService.available
    readonly property bool servicePending: !!resolvedPowerService && resolvedPowerService.pending
    readonly property var display: resolvedPowerService ? resolvedPowerService.displayDevice : null
    readonly property bool present: serviceAvailable && display !== null && display.isPresent
    readonly property bool validCharge: present && Number.isFinite(display.percentage)
    readonly property int percentage: validCharge ? Math.round(Math.max(0, Math.min(100, display.percentage))) : 0
    readonly property bool charging: present && display.state === UPowerDevice.Charging
    readonly property bool low: validCharge && percentage <= 15 && display.state === UPowerDevice.Discharging
    readonly property bool canSavePower: controller !== null && controller.powerAvailable && controller.powerProfiles.indexOf("power-saver") >= 0
    Instantiator {
        id: localService
        active: root.serviceHost === null
        delegate: UPowerHost {}
    }
    UPowerDeviceModel {
        id: devices
        host: root.serviceHost === null ? localService.object : null
    }
    function duration(seconds): string {
        const minutes = Math.max(1, Math.round(seconds / 60));
        const hours = Math.floor(minutes / 60);
        return hours > 0 ? i18n("%1 h %2 min").arg(hours).arg(minutes % 60) : i18n("%1 min").arg(minutes);
    }
    function stateLine(device): string {
        if (!device)
            return "";
        if (device.state === UPowerDevice.Charging)
            return device.timeToFull > 0 ? i18n("Charging, %1 until full").arg(root.duration(device.timeToFull)) : i18n("Charging. Calculating time until full.");
        if (device.state === UPowerDevice.Discharging)
            return device.timeToEmpty > 0 ? i18n("%1 remaining").arg(root.duration(device.timeToEmpty)) : i18n("On battery. Calculating time remaining.");
        if (device.state === UPowerDevice.FullyCharged)
            return i18n("Fully charged");
        if (device.state === UPowerDevice.Empty)
            return i18n("Battery empty");
        if (device.state === UPowerDevice.PendingCharge)
            return i18n("Connected to power. Waiting to charge.");
        if (device.state === UPowerDevice.PendingDischarge)
            return i18n("Waiting to use battery power");
        return i18n("Battery status unavailable");
    }
    component Fact: RowLayout {
        property string label: ""
        property string value: ""
        width: parent ? parent.width : 0
        DetailText {
            Layout.fillWidth: true
            text: parent.label
            font.pixelSize: Tokens.font_size_body_s
            muted: true
        }
        DetailText {
            text: parent.value
            font.pixelSize: Tokens.font_size_body_s
        }
    }
    DetailEmptyState {
        objectName: "batteryUnavailable"
        visible: !root.serviceAvailable
        iconName: "battery-missing"
        title: root.servicePending ? i18n("Reading battery status") : i18n("Battery service unavailable")
        description: root.servicePending ? i18n("Checking the power supply and connected devices.") : i18n("Battery readings could not be loaded. Check the power service and try again.")
        actionText: root.servicePending ? "" : i18n("Try again")
        onActivated: if (root.resolvedPowerService)
            root.resolvedPowerService.refresh()
    }
    DetailEmptyState {
        objectName: "batteryAbsent"
        visible: root.serviceAvailable && !root.present
        iconName: "computer"
        title: i18n("No system battery")
        description: i18n("This computer has no battery to report. Connected devices appear below when they share a charge level.")
    }
    DetailNotice {
        objectName: "batteryLow"
        text: root.low ? i18n("Battery is low. Connect a charger or use Power saver to reduce power use.") : ""
        error: true
    }
    DetailCard {
        objectName: "batteryCharge"
        visible: root.present
        title: root.validCharge ? i18n("%1%").arg(root.percentage) : i18n("Charge unknown")
        titleSize: 32
        description: root.stateLine(root.display)
        iconName: root.display && root.display.iconName ? root.display.iconName : "battery"
        status: root.charging ? i18n("Charging") : root.low ? i18n("Low battery") : root.resolvedPowerService && root.resolvedPowerService.onBattery ? i18n("On battery") : i18n("Connected to power")
        Rectangle {
            width: parent.width
            height: Tokens.spacing_s
            radius: Tokens.radius_full
            color: Appearance.recess
            Accessible.role: Accessible.ProgressBar
            Accessible.name: root.validCharge ? i18n("Battery charge %1 percent").arg(root.percentage) : i18n("Battery charge unknown")
            Rectangle {
                width: parent.width * root.percentage / 100
                height: parent.height
                radius: parent.radius
                color: root.low ? Theme.error : root.charging ? Theme.success : Appearance.stops[0]
            }
        }
        Fact {
            visible: root.display !== null && root.display.healthPercentage > 0
            label: i18n("Battery health")
            value: root.display ? i18n("%1% of design capacity").arg(Math.round(root.display.healthPercentage)) : ""
        }
        Fact {
            visible: root.display !== null && root.display.energyRate > 0
            label: root.charging ? i18n("Charging rate") : i18n("Power draw")
            value: root.display ? i18n("%1 W").arg(root.display.energyRate.toFixed(1)) : ""
        }
        Fact {
            visible: root.display !== null && root.display.energyCapacity > 0
            label: i18n("Energy remaining")
            value: root.display ? i18n("%1 of %2 Wh").arg(root.display.energy.toFixed(1)).arg(root.display.energyCapacity.toFixed(1)) : ""
        }
    }
    DetailNotice {
        text: root.controller ? root.controller.powerError : ""
        error: true
    }
    ShellButton {
        objectName: "batteryPowerSaver"
        visible: root.low && root.canSavePower && root.controller.powerProfile !== "power-saver"
        text: i18n("Use Power saver")
        iconName: "battery-profile-powersave"
        enabled: root.canSavePower && !root.controller.powerPending
        onClicked: root.controller.setPowerProfile("power-saver")
    }
    DetailDeviceRow {
        objectName: "batteryPowerProfile"
        visible: root.controller !== null
        title: i18n("Power profile")
        subtitle: !root.controller || !root.controller.powerAvailable ? i18n("Power profiles unavailable") : root.controller.powerProfile === "power-saver" ? i18n("Power saver") : root.controller.powerProfile === "performance" ? i18n("Performance") : i18n("Balanced")
        iconName: "preferences-system-power-management"
        onClicked: root.panelRequested("power")
    }
    DetailSectionHeading {
        visible: root.serviceAvailable && root.deviceModel.count > 0
        title: i18n("Battery details and devices")
    }
    Repeater {
        model: root.deviceModel
        delegate: DetailCard {
            required property var device
            visible: root.serviceAvailable && device !== null && device.isPresent && device.type !== UPowerDevice.LinePower
            title: device && device.model ? device.model : device && device.isLaptopBattery ? i18n("System battery") : i18n("Connected device")
            description: root.stateLine(device)
            iconName: device && device.iconName ? device.iconName : "battery"
            status: device && Number.isFinite(device.percentage) ? i18n("%1%").arg(Math.round(device.percentage)) : i18n("Unknown charge")
            horizontal: true
            titleSize: 13
            Fact {
                visible: device !== null && device.healthPercentage > 0
                label: i18n("Battery health")
                value: device ? i18n("%1%").arg(Math.round(device.healthPercentage)) : ""
            }
        }
    }
}
