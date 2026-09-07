// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.NetworkPanel, the network chip's panel.
//
// The Wi-Fi radio switch in the header, then every access point the first
// Wi-Fi device can see, strongest first, with a passphrase field for a
// secured network. Owns its own NetworkHost for the reason NetworkTile
// documents: NetworkHost is a thin view over NetworkManager's D-Bus
// state, so a second one costs a set of property mirrors, not a second
// connection's worth of traffic.
//
// The radio switch governs WIRELESS specifically, never NetworkManager's
// global networking switch: flipping the global one from a control
// labelled Wi-Fi would take an ethernet link down with it.
//
// Every write here is fire-and-forget. NetworkManager echoes the result
// back through the device's state and the manager's connectivity, so the
// panel shows what actually happened rather than what was asked for, and a
// refused connection simply leaves the row where it was.

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.Network

PanelFrame {
    id: root

    title: qsTr("Network")
    iconName: root._wifiDevice ? "network-wireless" : "network-wired"
    subtitle: {
        if (!host.networkingEnabled)
            return qsTr("Networking is off");
        if (!host.wirelessEnabled)
            return qsTr("Wi-Fi is off");
        if (host.primaryConnectionType.length === 0)
            return qsTr("Not connected");
        return host.primaryConnectionType.includes("wireless") ? qsTr("Connected over Wi-Fi") : qsTr("Connected over %1").arg(host.primaryConnectionType);
    }

    NetworkHost {
        id: host
    }

    // The first managed Wi-Fi device. NM exposes no "primary" wireless
    // device, and a machine with two radios is rare enough that picking the
    // first is the honest simplification; the panel says which interface it
    // is acting on so a two-radio box is not silently confusing.
    //
    // Recomputed on deviceCount, which NM emits per add AND per remove, so
    // a count-neutral replacement (remove then add) notifies twice and this
    // re-resolves each time rather than holding a freed device.
    readonly property NetworkDevice _wifiDevice: {
        // Referenced so the binding re-evaluates when devices come and go;
        // deviceAt() is a plain call and tracks no dependency of its own.
        const n = host.deviceCount;
        for (let i = 0; i < n; ++i) {
            const device = host.deviceAt(i);
            if (device && device.deviceType === NetworkDevice.Wifi && device.managed)
                return device;
        }
        return null;
    }

    // The access point the panel is asking a passphrase for, or null. One
    // at a time: a second press elsewhere replaces it, which is also how
    // the field is cancelled.
    property var _pendingAp: null

    AccessPointModel {
        id: accessPoints

        device: root._wifiDevice
    }

    // A scan on open, so the list is current rather than whatever the
    // daemon last happened to cache. Fire-and-forget; rows arrive as the
    // daemon reports them.
    Component.onCompleted: {
        if (host.wirelessEnabled)
            host.scanWifi();
    }

    headerAction: Component {
        PanelToggle {
            subject: qsTr("Wi-Fi")
            checked: host.wirelessEnabled
            // Networking disabled wholesale (airplane mode, or NM stopped)
            // leaves no radio to turn on, so the control goes inert rather
            // than offering a write NetworkManager will refuse.
            available: host.networkingEnabled
            onToggled: {
                // Captured BEFORE the write. setWirelessEnabled issues an
                // async Properties.Set and the cached value only moves when
                // the daemon echoes it back, so reading the property again
                // after the assignment still returns the old state — the
                // trap that would make this scan on the way down.
                const turningOn = !host.wirelessEnabled;
                host.wirelessEnabled = turningOn;
                if (turningOn)
                    host.scanWifi();
            }
        }
    }

    Text {
        width: parent.width
        visible: accessPoints.count === 0
        text: {
            if (!root._wifiDevice)
                return qsTr("No Wi-Fi device");
            if (!host.wirelessEnabled)
                return qsTr("Turn Wi-Fi on to see networks");
            return qsTr("Looking for networks…");
        }
        color: Theme.on_surface_variant
        font.pixelSize: Tokens.font_size_body_s
        font.family: Tokens.font_family_ui
        wrapMode: Text.Wrap
        topPadding: Tokens.spacing_s
        bottomPadding: Tokens.spacing_s
    }

    Repeater {
        model: accessPoints

        delegate: Column {
            id: apEntry

            required property var accessPoint
            required property string ssid
            required property int strength
            required property bool secured

            width: parent ? parent.width : 0
            spacing: 0

            // The device carries the association, so "connected" is the
            // device naming this access point's object path as its active
            // one. Matched on the PATH, not the SSID: several access points
            // in a mesh share one SSID, and matching by name would light
            // every one of them.
            readonly property bool _connected: root._wifiDevice !== null && apEntry.accessPoint !== null && root._wifiDevice.activeAccessPointPath !== "" && root._wifiDevice.activeAccessPointPath === apEntry.accessPoint.dbusPath

            PanelRow {
                width: parent.width
                // The four-bar family, so the row's glyph carries the
                // strength as well as the trailing readout does.
                iconName: {
                    if (apEntry.strength >= 75)
                        return "network-wireless-signal-excellent";
                    if (apEntry.strength >= 50)
                        return "network-wireless-signal-good";
                    if (apEntry.strength >= 25)
                        return "network-wireless-signal-ok";
                    return "network-wireless-signal-weak";
                }
                label: apEntry.ssid.length > 0 ? apEntry.ssid : qsTr("Hidden network")
                sublabel: apEntry.secured ? qsTr("Secured") : qsTr("Open")
                trailingText: apEntry.strength + "%"
                current: apEntry._connected
                actionName: apEntry._connected ? qsTr("Connected to %1").arg(apEntry.ssid) : qsTr("Connect to %1").arg(apEntry.ssid)
                onClicked: root._press(apEntry.accessPoint, apEntry.secured)
            }

            // The passphrase field, shown under the row it belongs to so it
            // is unambiguous which network is being joined. Only ever one is
            // open, since _pendingAp holds a single access point.
            RowLayout {
                id: passphraseRow

                width: parent.width
                visible: root._pendingAp === apEntry.accessPoint
                spacing: Tokens.spacing_xs

                PhosphorTextField {
                    id: passphrase

                    Layout.fillWidth: true
                    placeholderText: qsTr("Password")
                    echoMode: TextInput.Password
                    onAccepted: root._connect(apEntry.accessPoint, passphrase.text)
                }

                // The field takes focus as it appears, so joining a network
                // is press-then-type with nothing in between. Bound to the
                // ROW's visibility rather than the field's own: the field is
                // inside the row, so its `visible` reads as effective and
                // would already be false here for the same reason.
                Connections {
                    target: passphraseRow

                    function onVisibleChanged(): void {
                        if (passphraseRow.visible)
                            passphrase.forceActiveFocus();
                    }
                }

                PhosphorButton {
                    text: qsTr("Join")
                    // Outlined, not Filled: R1 keeps colour out of a button
                    // background, and an outline is a stroke.
                    variant: PhosphorButton.Outlined
                    onClicked: root._connect(apEntry.accessPoint, passphrase.text)
                }
            }
        }
    }

    function _press(accessPoint, secured) {
        if (!root._wifiDevice || !accessPoint)
            return;
        if (!secured) {
            root._connect(accessPoint, "");
            return;
        }
        // Toggle: pressing the open row again puts the field away.
        root._pendingAp = root._pendingAp === accessPoint ? null : accessPoint;
    }

    function _connect(accessPoint, pass) {
        if (!root._wifiDevice || !accessPoint)
            return;
        host.connectToAccessPoint(root._wifiDevice, accessPoint, pass);
        root._pendingAp = null;
    }
}
