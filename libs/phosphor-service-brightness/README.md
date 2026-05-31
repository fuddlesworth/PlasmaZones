<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-brightness

Display and keyboard backlight brightness for Phosphor-based desktop shells.

## Responsibility

Exposes the brightness-controllable backlights (display panels under
`/sys/class/backlight`, keyboard backlights under `/sys/class/leds`) as Qt +
QML types. No UI; the shell decides how a brightness slider or OSD is rendered.

The read path is sysfs (with a file watcher tracking external changes); the
write path is logind's `org.freedesktop.login1.Session.SetBrightness`, so no
root or udev rule is required. Only display and keyboard backlights are
surfaced; the other `/sys/class/leds` entries (capslock / power / RGB /
trigger indicators) are not brightness controls and are skipped.

## Key types

| Type                    | Role                                                                                              |
|-------------------------|---------------------------------------------------------------------------------------------------|
| `BrightnessDevice`      | One backlight. `name`, `kind` (Display / Keyboard), `brightness`, `maxBrightness`, `percentage`; `setBrightness()`, `setPercentage()` (fire-and-forget via logind). |
| `BrightnessHost`        | Enumerates the brightness devices under a sysfs root and resolves the logind session for writes. `deviceCount`, `deviceAt()`, `devices()`. |
| `BrightnessDeviceModel` | `QAbstractListModel` over the host's devices. Roles: `device`, `name`, `kind`, `brightness`, `maxBrightness`, `percentage`. |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceBrightness/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceBrightness::registerQmlTypes();
    // ... load shell.qml
}
```

QML consumer (a brightness slider per device):

```qml
import Phosphor.Service.Brightness 1.0

BrightnessHost { id: bright }
BrightnessDeviceModel { id: devices; host: bright }

Repeater {
    model: devices
    delegate: Slider {
        from: 0; to: maxBrightness; value: brightness
        onMoved: device.setBrightness(value)
    }
}
```

## Design notes

- **sysfs read, logind write.** The current value comes from the sysfs
  `brightness` attribute (`max_brightness` for the range); writes go through
  logind `Session.SetBrightness(subsystem, name, value)` with the matching
  subsystem (`backlight` for display, `leds` for keyboard), so the lib needs
  no root and no udev rules. With no logind session bound the write no-ops and
  the lib stays read-only.
- **No optimistic writes.** `setBrightness` / `setPercentage` issue the logind
  call and let the file watcher pick up the applied value; the cached
  `brightness` (and its NOTIFY) moves only once sysfs actually changes.
- **Scope is brightness, not LEDs.** Display backlights plus the
  `kbd_backlight` entries under `/sys/class/leds`; every other LED (indicator,
  RGB, trigger) is skipped. A general LED-control surface, if ever wanted, is a
  separate concern.
- **Divide-by-zero safe.** A device whose `max_brightness` is 0 or missing
  surfaces a 0 percentage and an inert write rather than crashing.
- **Injectable for testing.** The sysfs root and the
  `(connection, service, sessionPath)` are constructor-injectable, so the
  whole read + write surface is exercised against a fake sysfs tree and a fake
  logind with no root and no daemon.

## Dependencies

- Linux sysfs (`/sys/class/backlight`, `/sys/class/leds`).
- A running `org.freedesktop.login1` (logind) on the system bus for the write
  path (the lib loads read-only inert without it).
- `PhosphorDBus` (private link; the logind `SetBrightness` call).
- Qt6 ≥ 6.6 (Core, Qml, DBus). No QtGui.

## Status

Phase 2.4: shipped. The sysfs read path with live `QFileSystemWatcher` updates
(`BrightnessDevice`), the logind write path (`setBrightness` / `setPercentage`
via `Session.SetBrightness`), enumeration of display + `kbd_backlight` devices
with the indicator-LED exclusion (`BrightnessHost`), the `QAbstractListModel`
(`BrightnessDeviceModel`), and the `phosphorctl`-style CLI demo
(`examples/phosphor-service-brightness-cli`: list, get, set) covering the
Phase-2 gate caps (list devices, get/set brightness) all landed. The smoke
harness pins enumeration, the `kbd_backlight` filter, percentage math,
divide-by-zero safety, live watcher updates, model role names, and the logind
write path deterministically against a fake sysfs tree and a fake logind over
a session-bus loopback, no root or daemon required.
