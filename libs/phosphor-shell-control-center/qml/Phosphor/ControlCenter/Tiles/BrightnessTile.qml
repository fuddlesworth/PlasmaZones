// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.BrightnessTile, the display brightness slider.
//
// Drives the first Display-kind device: the internal panel backlight, or
// an external display when there is no internal panel. Keyboard backlights
// are deliberately not what a tile labelled "Brightness" writes, so they are
// filtered out here and belong to the detail view, which can list every
// device.
//
// A desktop with no internal panel and no DDC/CI monitor has no device at
// all, and the tile goes inert rather than pretending to a control.

import QtQuick
import Phosphor.ControlCenter
import Phosphor.Service.Brightness

SliderTile {
    id: root

    BrightnessHost {
        id: host
    }

    // Prefer the internal panel; fall back to an external display so a
    // desktop with a DDC/CI-capable monitor still gets a working tile.
    //
    // No revision counter is needed to keep this binding honest, unlike
    // the Bluetooth adapter: the host's header states sysfs devices are
    // enumerated once at construction and DDC/CI enumeration is one-shot,
    // so a device object is never swapped underneath a stable count. The
    // deviceCount dependency covers the async DDC/CI arrival.
    readonly property BrightnessDevice _device: {
        let external = null;
        for (let i = 0; i < host.deviceCount; ++i) {
            const device = host.deviceAt(i);
            if (!device)
                continue;
            if (device.kind === BrightnessDevice.Display)
                return device;
            if (device.kind === BrightnessDevice.ExternalDisplay && !external)
                external = device;
        }
        return external;
    }

    iconName: {
        if (root.value < 34)
            return "brightness-low";
        return root.value < 67 ? "brightness-medium" : "brightness-high";
    }
    label: qsTr("Brightness")
    from: 0
    to: 100
    // percentage is 0..1 from the service; the tile works in whole percent
    // so the readout and the slider share one scale.
    value: root._device ? Math.round(root._device.percentage * 100) : 0
    available: root._device !== null
    // Only worth drilling into when there is more than one device to pick
    // between; a laptop with a single panel has nothing to show.
    hasDetail: host.deviceCount > 1

    onMoved: v => {
        if (root._device)
            root._device.setPercentage(v / 100);
    }
}
