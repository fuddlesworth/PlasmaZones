<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-upower

UPower (`org.freedesktop.UPower`) battery and power-supply readouts for Phosphor-based desktop shells.

## Responsibility

Exposes the system bus `org.freedesktop.UPower` surface — the aggregate display device, the per-device list, the `OnBattery` flag — as Qt + QML types. No UI; the shell decides how a battery percentage, charging icon, or low-power warning is rendered.

The library is a pure D-Bus client. It does not write to UPower (UPower has no writable surface for the kinds of facts it reports), it only observes.

## Key types

| Type                          | Role                                                                                 |
|-------------------------------|--------------------------------------------------------------------------------------|
| `UPowerDevice`                | One physical power source. `percentage`, `state`, `type`, `timeToEmpty`, `timeToFull`, `iconName`, `isLaptopBattery`, `healthPercentage`. |
| `UPowerHost`                  | Owns the device set. `onBattery`, `displayDevice` (the UPower-aggregate battery), `deviceCount`, signals on add/remove. |
| `UPowerDeviceModel`           | `QAbstractListModel` over the host's devices. Roles: `device`, `percentage`, `deviceState`, `deviceType`, `iconName`, `isLaptopBattery`. |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceUPower/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceUPower::registerQmlTypes();
    // ... load shell.qml
}
```

QML consumer (single-battery indicator):

```qml
import Phosphor.Service.UPower 1.0

UPowerHost { id: battery }

Label {
    visible: battery.displayDevice !== null
    text: battery.displayDevice
        ? Math.round(battery.displayDevice.percentage) + "%"
        : ""
}
```

QML consumer (per-device list):

```qml
import Phosphor.Service.UPower 1.0

UPowerHost { id: battery }
UPowerDeviceModel { id: devices; host: battery }

Repeater {
    model: devices
    delegate: Label { text: iconName + " " + Math.round(percentage) + "%" }
}
```

## Design notes

- **Async-only D-Bus.** Startup (`GetAll`, `EnumerateDevices`, `GetDisplayDevice`) and `PropertiesChanged` fan-out all go through `QDBusPendingCallWatcher`. The GUI thread is never blocked waiting on UPower.
- **Derived-state transitions.** `isLaptopBattery` depends on both `type` and `powerSupply`; `healthPercentage` on `energyFull` and `energyFullDesign`. The applyProps path snapshots the prior derived value and emits the NOTIFY only when the result actually moved, so QML bindings don't churn on every device-level property tick.
- **Display device.** UPower returns a single "display" device aggregating every battery on the system. Shells should prefer `displayDevice` for top-of-bar indicators and reserve the model for power-management UIs that want the per-device breakdown.

## Dependencies

- Qt6 ≥ 6.6 (Core, Qml, DBus)
- A running `org.freedesktop.UPower` on the system bus

## Status

Phase 2.0 extraction from the original `phosphor-services` umbrella. The C++ + QML API is unchanged from its pre-extraction form (`PhosphorServices::UPower*` → `PhosphorServiceUPower::UPower*`, `Phosphor.Services` QML module → `Phosphor.Service.UPower`).
