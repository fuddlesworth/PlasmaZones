// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.BluetoothPanel, the Bluetooth chip's panel.
//
// The first adapter's power switch in the header, then the devices BlueZ
// knows about: connected first, then paired, then whatever discovery has
// turned up. A press connects or disconnects; an unpaired device is paired
// first, which BlueZ drives through the agent BluetoothHost owns.
//
// Discovery runs only while the panel is open. It costs radio time and
// drains a laptop battery, so leaving it on after the panel closes would
// be a cost with nothing on screen to justify it.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.Bluetooth

PanelFrame {
    id: root

    title: qsTr("Bluetooth")
    iconName: root._powered ? "network-bluetooth-activated" : "network-bluetooth"
    subtitle: {
        if (!root.adapter)
            return qsTr("No adapter");
        if (!root._powered)
            return qsTr("Turned off");
        if (root._connectedCount === 1)
            return qsTr("1 device connected");
        if (root._connectedCount > 1)
            return qsTr("%1 devices connected").arg(root._connectedCount);
        return root.adapter.discovering ? qsTr("Looking for devices…") : qsTr("No devices connected");
    }

    // Named btHost, not host: BluetoothDeviceModel has a property of its
    // own called `host`, and `host: host` inside that object would resolve
    // the right-hand side to the model's own property rather than to this
    // id, binding it to itself.
    BluetoothHost {
        id: btHost
    }

    // The first adapter. A machine with two is rare enough that picking the
    // first is the honest simplification, and adapterCount notifies on both
    // add and remove so this re-resolves rather than holding a freed one.
    readonly property BluetoothAdapter adapter: btHost.adapterCount > 0 ? btHost.adapterAt(0) : null
    readonly property bool _powered: root.adapter ? root.adapter.powered : false

    BluetoothDeviceModel {
        id: devices

        host: btHost
        adapter: root.adapter
    }

    // Walks the HOST's devices, not the model's rows: the model is filtered
    // by adapter, so its row i is not host device i and indexing one with
    // the other's count would read the wrong device (or past the end).
    //
    // Re-evaluates on deviceCount only. A device connecting does not change
    // that count, so this line can lag by one connect until the next add or
    // remove. Accepted deliberately: it is the header's summary sentence,
    // and the rows below bind each device's own `connected` and are always
    // current. Making it exact would mean a per-device signal fan-in that
    // QML cannot express without a helper object.
    readonly property int _connectedCount: {
        let n = 0;
        const total = btHost.deviceCount;
        for (let i = 0; i < total; ++i) {
            const device = btHost.deviceAt(i);
            if (device && device.connected)
                ++n;
        }
        return n;
    }

    // Discovery for the life of the panel, and only while the radio is on:
    // BlueZ refuses StartDiscovery on an unpowered adapter.
    Component.onCompleted: {
        if (root.adapter && root._powered)
            root.adapter.startDiscovery();
    }
    Component.onDestruction: {
        if (root.adapter && root.adapter.discovering)
            root.adapter.stopDiscovery();
    }

    headerAction: Component {
        PanelToggle {
            subject: qsTr("Bluetooth")
            checked: root._powered
            available: root.adapter !== null
            onToggled: {
                if (!root.adapter)
                    return;
                // Captured before the write for the same reason the network
                // panel captures it: setPowered is an async D-Bus write and
                // the cached property only moves when BlueZ echoes it back.
                const turningOn = !root.adapter.powered;
                root.adapter.setPowered(turningOn);
                if (turningOn)
                    root.adapter.startDiscovery();
            }
        }
    }

    Text {
        width: parent.width
        visible: devices.count === 0
        text: root._powered ? qsTr("Looking for devices…") : qsTr("Turn Bluetooth on to see devices")
        color: Theme.on_surface_variant
        font.pixelSize: Tokens.font_size_body_s
        font.family: Tokens.font_family_ui
        wrapMode: Text.Wrap
        topPadding: Tokens.spacing_s
        bottomPadding: Tokens.spacing_s
    }

    Repeater {
        model: devices

        delegate: PanelRow {
            id: deviceRow

            // Only the object is required from the model. Every field below
            // reads off it, which keeps the delegate independent of the
            // model's role names — and avoids requiring a role literally
            // named `alias`, which QML reserves for property aliases and
            // cannot be used as a property name here.
            required property var device

            readonly property bool _paired: deviceRow.device ? deviceRow.device.paired : false
            readonly property bool _connected: deviceRow.device ? deviceRow.device.connected : false
            // Alias over name: BlueZ's Alias is the user-renamed label when
            // there is one and the device's own name otherwise, so it is
            // never empty where name would be.
            readonly property string _label: deviceRow.device ? deviceRow.device.alias : ""

            width: parent ? parent.width : 0
            // BlueZ's Icon is already a freedesktop name ("audio-headset",
            // "input-mouse"), so it needs no lookup table here. It is empty
            // for a device that publishes no class, which the fallback
            // covers rather than drawing the unknown-icon glyph.
            iconName: deviceRow.device && deviceRow.device.icon !== "" ? deviceRow.device.icon : "network-bluetooth"
            label: deviceRow._label
            sublabel: {
                if (deviceRow._connected)
                    return qsTr("Connected");
                return deviceRow._paired ? qsTr("Paired") : qsTr("Not paired");
            }
            current: deviceRow._connected
            actionName: {
                if (deviceRow._connected)
                    return qsTr("Disconnect %1").arg(deviceRow._label);
                return deviceRow._paired ? qsTr("Connect %1").arg(deviceRow._label) : qsTr("Pair %1").arg(deviceRow._label);
            }
            onClicked: {
                if (!deviceRow.device)
                    return;
                if (deviceRow._connected) {
                    deviceRow.device.disconnectDevice();
                    return;
                }
                // Pairing first for an unpaired device. BlueZ connects as
                // part of a successful pairing, so this is not followed by a
                // connect call: issuing both would race the agent's
                // confirmation dialogue.
                if (deviceRow._paired)
                    deviceRow.device.connectDevice();
                else
                    deviceRow.device.pair();
            }
        }
    }
}
