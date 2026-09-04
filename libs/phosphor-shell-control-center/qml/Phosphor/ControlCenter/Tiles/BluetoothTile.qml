// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.BluetoothTile, the Bluetooth power toggle.
//
// Owns a BluetoothHost and drives the FIRST adapter's `powered` property.
// Most machines have exactly one; a second adapter needs the detail view
// to pick between them, which is why this tile carries a chevron.
//
// The host is inert without a bus (no bluetoothd → empty lists, no
// crash), so a box with the daemon stopped shows a dimmed tile rather
// than an error.

import QtQuick
import Phosphor.ControlCenter
import Phosphor.Service.Bluetooth

Tile {
    id: root

    BluetoothHost {
        id: host
    }

    // Bumped whenever the adapter set changes, and read by the binding
    // below purely so the binding re-evaluates.
    //
    // `adapterAt(0)` is a plain function call that tracks no dependency,
    // and pairing it with `adapterCount` alone does not fix that: a
    // bluetoothd restart that lands on the same number of adapters
    // replaces every adapter object without moving the count, and a
    // count-gated binding would stay pinned to an adapter owned by the
    // destroyed connection. Same failure the bar's Audio widget documents
    // for PipeWire nodes, which solves it with a `firstNode` accessor the
    // Bluetooth host does not have.
    property int _adapterRevision: 0

    readonly property BluetoothAdapter _adapter: {
        // Referenced so the binding re-runs on a swap; the value is unused.
        void root._adapterRevision;
        return host.adapterCount > 0 ? host.adapterAt(0) : null;
    }

    readonly property bool _powered: root._adapter ? root._adapter.powered : false

    readonly property int _connectedCount: {
        void root._adapterRevision;
        let n = 0;
        for (let i = 0; i < host.deviceCount; ++i) {
            const device = host.deviceAt(i);
            if (device && device.connected)
                ++n;
        }
        return n;
    }

    Connections {
        target: host

        function onAdapterAdded(adapter: var): void {
            root._adapterRevision++;
        }

        function onAdapterRemoved(adapter: var): void {
            root._adapterRevision++;
        }
    }

    iconName: root._powered ? "network-bluetooth" : "network-bluetooth-inactive"
    label: qsTr("Bluetooth")
    sublabel: {
        if (!root._adapter)
            return qsTr("No adapter");
        if (!root._powered)
            return qsTr("Off");
        if (root._connectedCount === 0)
            return qsTr("On");
        // qsTr's own plural form, not KDE's i18np: the phosphor-* libraries
        // are standalone and translate through Qt, the way every sibling
        // shell widget does.
        // Qt uses the SOURCE string verbatim when no catalog is loaded, so a
        // "%n device(s)" msgid renders literally as "2 device(s)" in an
        // untranslated session. Branch explicitly instead.
        return root._connectedCount === 1 ? qsTr("1 device") : qsTr("%1 devices").arg(root._connectedCount);
    }
    active: root._powered
    // No adapter means nothing to power on.
    available: root._adapter !== null
    // No detailContent yet, so no chevron: hasDetail derives from whether
    // there is something to show. Give this tile a `detailContent`
    // Component (and a `detailTitle`) and the affordance returns.

    onToggled: {
        if (root._adapter)
            root._adapter.setPowered(!root._powered);
    }
}
