<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-bluetooth

BlueZ (`org.bluez`) adapter / device readouts and pairing for Phosphor-based desktop shells.

## Responsibility

Exposes the system bus `org.bluez` surface (per-adapter power / discovery state, per-device pairing / connection state, and the pairing agent) as Qt + QML types. No UI. The shell decides how an adapter toggle, device list, or pairing dialog is rendered.

The library is a D-Bus client built on the generic `PhosphorDBus::Client` async helper and `PhosphorDBus::ObjectManager` (BlueZ is ObjectManager-rooted). It observes adapter + device state, offers the discovery / connect / trust / pair write surface, and acts as a D-Bus *server* for the `org.bluez.Agent1` pairing agent.

## Key types

| Type                    | Role                                                                                                  |
|-------------------------|-------------------------------------------------------------------------------------------------------|
| `BluetoothAdapter`      | One `org.bluez.Adapter1`. `address`, `name`, `alias`, `powered`, `discoverable`, `pairable`, `discovering`; `setPowered()`, `setDiscoverable()`, `startDiscovery()`, `stopDiscovery()`, `removeDevice()`. |
| `BluetoothDevice`       | One `org.bluez.Device1`. `address`, `name`, `alias`, `icon`, `paired`, `trusted`, `blocked`, `connected`, `rssi`, `adapter`, `uuids`; `connectDevice()`, `disconnectDevice()`, `setTrusted()`, `setBlocked()`, `pair()`, `cancelPairing()`. |
| `BluetoothHost`         | Owns the adapter + device sets via an ObjectManager observer and registers the pairing agent. `adapterCount`, `deviceCount`, `agent`; `adapterAt()`, `deviceAt()`; signals on adapter/device add/remove. |
| `BluetoothAdapterModel` | `QAbstractListModel` over the host's adapters. Roles: `adapter`, `address`, `name`, `alias`, `powered`, `discoverable`, `pairable`, `discovering`. |
| `BluetoothDeviceModel`  | `QAbstractListModel` over the host's devices, optionally scoped to one `adapter`. Roles: `device`, `address`, `name`, `alias`, `icon`, `paired`, `trusted`, `blocked`, `connected`, `rssi`, `adapter`, `uuids`. |
| `BluetoothAgent`        | `org.bluez.Agent1` (KeyboardDisplay). Emits `pinCodeRequested` / `passkeyRequested` / `confirmationRequested` / `authorizationRequested` / `serviceAuthorizationRequested` (+ display signals); answered via `respondPinCode()` / `respondPasskey()` / `respondConfirmation()` / `rejectRequest()`. |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceBluetooth/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceBluetooth::registerQmlTypes();
    // ... load shell.qml
}
```

QML consumer (adapter power toggle + device list):

```qml
import Phosphor.Service.Bluetooth 1.0

BluetoothHost { id: bt }
BluetoothDeviceModel { id: devices; host: bt }

Switch {
    text: "Bluetooth"
    enabled: bt.adapterCount > 0
    checked: bt.adapterCount > 0 && bt.adapterAt(0).powered
    onToggled: if (bt.adapterCount > 0) bt.adapterAt(0).setPowered(checked)
}

Repeater {
    model: devices
    delegate: Label { text: (alias || name || address) + (connected ? " — connected" : "") }
}
```

QML consumer (pairing dialog wired to the agent):

```qml
import Phosphor.Service.Bluetooth 1.0

BluetoothHost { id: bt }

Connections {
    target: bt.agent
    function onConfirmationRequested(devicePath, passkey, requestId) {
        // show a dialog displaying `passkey`, then:
        //   bt.agent.respondConfirmation(requestId, accepted)
    }
}
```

## Design notes

- **ObjectManager-rooted enumeration.** Adapters and devices are discovered through `PhosphorDBus::ObjectManager` pointed at the BlueZ root (`GetManagedObjects` + `InterfacesAdded` / `InterfacesRemoved`), not per-interface signals. Removing an adapter cascades to its child devices by path prefix.
- **Async-only D-Bus.** The initial walk, per-object `PropertiesChanged` tracking, and every write (`Properties.Set`, `StartDiscovery`, `Connect`, `Pair`, `RemoveDevice`) go through `QDBusPendingCallWatcher` / `PhosphorDBus::Client`. The GUI thread is never blocked. Initial properties arrive in the ObjectManager enumeration, so domain objects need no construction-time round-trip.
- **No optimistic writes.** `setPowered` / `setDiscoverable` / `setTrusted` / `setBlocked` issue `Properties.Set` and let BlueZ echo the change back through `PropertiesChanged`. The cached value (and its NOTIFY) moves only once the daemon actually applied it, so the property never lies about daemon state.
- **The agent is a D-Bus server.** `BluetoothAgent` implements `org.bluez.Agent1` directly on the QObject (`ExportAllSlots` + `QDBusContext`) rather than via a generated adaptor, so `setDelayedReply` applies to its calls. The interactive callbacks defer their reply, emit a request signal with a `requestId`, and are answered later by a consumer. Replies are sent exactly once (value, empty accept, or `org.bluez.Error.Rejected`), and `Cancel` answers every in-flight request with `Canceled`.
- **Best-effort default agent.** The host registers the agent with `AgentManager1.RegisterAgent` (KeyboardDisplay) and requests default-agent status. If `RequestDefaultAgent` fails because another agent holds the slot, that is non-fatal, and pairing still works as a non-default agent.

## Dependencies

- Qt6 ≥ 6.6 (Core, Qml, DBus)
- `PhosphorDBus` (private link; async method-call helper + ObjectManager observer)
- A running `org.bluez` on the system bus (the lib loads inert without it, with empty adapter and device lists and no crash)

## Status

Shipped. The ObjectManager-driven adapter / device enumeration with live `PropertiesChanged` tracking and cascade removal (`BluetoothHost`, `BluetoothAdapter`, `BluetoothDevice`), the two `QAbstractListModel`s (`BluetoothAdapterModel`, `BluetoothDeviceModel` with an adapter filter), the write paths (power / discoverable / discovery, connect / disconnect / trust / block / pair / cancel-pairing / remove), the full `org.bluez.Agent1` pairing agent (`BluetoothAgent`, KeyboardDisplay, delayed-reply interactive callbacks), and the `phosphorctl`-style CLI demo (`examples/phosphor-service-bluetooth-cli`: status, list-adapters, list-devices, power, scan, pair, connect, disconnect, trust, untrust, remove) covering the core capabilities (list adapters / devices, scan, pair, connect). The ObjectManager observer added for this service lives in `phosphor-dbus` and is reused by other services (notifications, session). The smoke harness pins the public contract (model role names, lifecycle, the agent request/response state machine, and a delayed-reply round-trip) deterministically over a session-bus loopback without a daemon. The `phosphor-dbus` ObjectManager has its own loopback test.
