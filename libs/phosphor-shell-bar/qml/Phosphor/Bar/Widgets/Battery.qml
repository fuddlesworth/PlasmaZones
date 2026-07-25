// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Battery, the battery charge bar widget.
//
// Self-contained: owns a UPowerHost and reads its aggregate
// displayDevice. Hides itself entirely on a desktop with no battery (no
// present device, or a non-finite percentage during bus init). The
// freedesktop icon name comes from UPower itself, so the glyph tracks
// charge level and charging state without a local lookup table.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.UPower

Item {
    id: root

    // A battery is shown only when UPower reports a present device with a
    // finite percentage. isPresent gates out the synthetic display device
    // a desktop with no battery still exposes (it reports
    // battery-missing-symbolic); Number.isFinite rejects the NaN a
    // transient bus-init or mid-poll disconnect can surface.
    readonly property bool available: !!battery.displayDevice && battery.displayDevice.isPresent && Number.isFinite(battery.displayDevice.percentage)
    readonly property int percent: available ? Math.round(battery.displayDevice.percentage) : 0
    readonly property bool charging: available && battery.displayDevice.state === UPowerDevice.Charging

    visible: root.available
    implicitWidth: root.available ? row.implicitWidth : 0
    implicitHeight: row.implicitHeight

    UPowerHost {
        id: battery
    }

    Accessible.role: Accessible.Indicator
    Accessible.name: "Battery " + root.percent + " percent" + (root.charging ? ", charging" : "")

    Row {
        id: row

        spacing: Tokens.spacing_xs

        Kirigami.Icon {
            // UPower hands back a freedesktop battery icon name that already
            // encodes level + charging state. Tinted to a single colour so
            // it reads on the dark capsule.
            width: 18
            height: 18
            source: root.available && battery.displayDevice.iconName ? battery.displayDevice.iconName : "battery"
            isMask: true
            color: root.charging ? Theme.success : Theme.on_surface
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            // Folded into the root's Accessible.name already; QQuickText
            // exposes itself as its own StaticText node, so without this
            // assistive tech reads the composed name and then re-reads
            // this fragment.
            Accessible.ignored: true
            text: root.percent + "%"
            color: root.charging ? Theme.success : Theme.on_surface
            font.pixelSize: Tokens.font_size_label_l
            font.family: Tokens.font_family
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
