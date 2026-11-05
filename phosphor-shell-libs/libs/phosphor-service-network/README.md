<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-network

NetworkManager (`org.freedesktop.NetworkManager`) device and connectivity readouts for Phosphor-based desktop shells.

## Responsibility

Exposes the system bus `org.freedesktop.NetworkManager` surface (per-device interface/type/state, the global connectivity level, the networking and Wi-Fi radio toggles) as Qt + QML types. No UI. The shell decides how a connectivity icon, device list, or Wi-Fi toggle is rendered.

The library is a D-Bus client built on the generic `PhosphorDBus::Client` async helper. It observes manager + device state and offers a small write surface (toggle the Wi-Fi radio, trigger a Wi-Fi scan).

## Key types

| Type                  | Role                                                                                                  |
|-----------------------|-------------------------------------------------------------------------------------------------------|
| `NetworkDevice`       | One `org.freedesktop.NetworkManager.Device`. `interfaceName`, `deviceType`, `state`, `managed`.       |
| `NetworkHost`         | Owns the device set + manager state. `networkingEnabled`, `wirelessEnabled` (writable), `connectivity`, `primaryConnectionType`, `deviceCount`; `scanWifi()`, `activateConnection()`, `connectToAccessPoint()`; signals on device add/remove. |
| `NetworkDeviceModel`  | `QAbstractListModel` over the host's devices. Roles: `device`, `interfaceName`, `deviceType`, `deviceState`, `managed`. |
| `AccessPoint`         | One `org.freedesktop.NetworkManager.AccessPoint`. `ssid`, `strength`, `frequency`, `bssid`, `security`, `secured`. |
| `AccessPointModel`    | `QAbstractListModel` over a Wi-Fi device's scanned APs (bind `device`). Roles: `accessPoint`, `ssid`, `strength`, `frequency`, `bssid`, `security`, `secured`. |
| `NetworkConnection`   | One saved profile (`Settings.Connection`). `id`, `uuid`, `connectionType`. |
| `NetworkConnectionModel` | `QAbstractListModel` over NetworkManager's saved connections (self-bootstrapping). Roles: `connection`, `id`, `uuid`, `connectionType`. |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceNetwork/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceNetwork::registerQmlTypes();
    // ... load shell.qml
}
```

QML consumer (connectivity indicator + Wi-Fi toggle):

```qml
import Phosphor.Service.Network 1.0

NetworkHost { id: net }

Switch {
    text: "Wi-Fi"
    checked: net.wirelessEnabled
    onToggled: net.wirelessEnabled = checked
}

Label {
    text: net.connectivity === NetworkHost.Full ? "Online" : "Limited / offline"
}
```

QML consumer (per-device list):

```qml
import Phosphor.Service.Network 1.0

NetworkHost { id: net }
NetworkDeviceModel { id: devices; host: net }

Repeater {
    model: devices
    delegate: Label { text: interfaceName + " (type " + deviceType + ", state " + deviceState + ")" }
}
```

## Design notes

- **Async-only D-Bus.** The manager bootstrap (`Properties.GetAll`, `GetDevices`), per-device `Properties.GetAll`, the `PropertiesChanged` fan-out, and the writes (`Set WirelessEnabled`, `Device.Wireless.RequestScan`) all go through `QDBusPendingCallWatcher` / `PhosphorDBus::Client`. The GUI thread is never blocked.
- **No optimistic writes.** `setWirelessEnabled` issues `Properties.Set` and lets NetworkManager echo the change back through `PropertiesChanged`. The cached `wirelessEnabled` flips (and the NOTIFY fires) only once the radio actually toggled, so the property never lies about daemon state.
- **Sparse wire enums.** `DeviceType` (3/4 unused, jumps to `Wireguard=29`) and `DeviceState` (multiples of ten) are mapped value-by-value. An unrecognised raw value surfaces as `Unknown` / `UnknownState` rather than casting to an enumerator the lib doesn't declare. `Connectivity` is contiguous 0..4 and clamped.
- **Path containment.** Device add/remove rejects any object path outside `/org/freedesktop/NetworkManager/Devices/` at the boundary, so a misbehaving daemon can't spin up a `NetworkDevice` on a nonsense path.

## Dependencies

- Qt6 ≥ 6.6 (Core, Qml, DBus)
- `PhosphorDBus` (private link; async method-call helper)
- A running `org.freedesktop.NetworkManager` on the system bus (the lib loads inert without it, with an empty device list and `UnknownConnectivity`)

## Status

Shipped. The manager lifecycle (connectivity / radio-toggle state + device enumeration), `NetworkDevice`, `NetworkDeviceModel`, the Wi-Fi radio toggle and `scanWifi()` trigger, access-point surfacing (`AccessPoint` / `AccessPointModel` with derived security labels), the saved-connection list (`NetworkConnection` / `NetworkConnectionModel`), the connect paths (`activateConnection()` for a saved profile, `connectToAccessPoint()` via NM AddAndActivateConnection for open or WPA-PSK networks), and the `phosphorctl`-style CLI demo (`examples/phosphor-service-network-cli`: status, list-devices, list-connections, list-aps, scan, connect) covering the core capabilities (list connections, scan wifi, connect to AP). Live D-Bus integration tests against a fake NetworkManager fixture are a follow-up. The smoke harness pins the public contract (role names, enum wire constants, lifecycle) deterministically without a daemon.
