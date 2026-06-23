<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-brightness

Display and keyboard backlight brightness for Phosphor-based desktop shells.

## Responsibility

Exposes the brightness-controllable backlights (display panels under
`/sys/class/backlight`, keyboard backlights under `/sys/class/leds`) plus
external monitors over DDC/CI as Qt + QML types. No UI. The shell decides how a
brightness slider or OSD is rendered.

For internal panels and keyboard backlights the read path is sysfs (with a file
watcher tracking external changes) and the write path is logind's
`org.freedesktop.login1.Session.SetBrightness`, so no root or udev rule is
required. Only display and keyboard backlights are surfaced. The other
`/sys/class/leds` entries (capslock / power / RGB / trigger indicators) are not
brightness controls and are skipped.

External monitors (which have no `/sys/class/backlight` entry) are surfaced over
DDC/CI on I2C, alongside the sysfs devices, as a second source inside the same
host. This is compile-time optional (see Design notes).

## Key types

| Type                    | Role                                                                                              |
|-------------------------|---------------------------------------------------------------------------------------------------|
| `BrightnessDevice`      | One backlight or external monitor. `id` (stable unique key: sysfs name for backlights, I2C bus like `i2c-7` for external displays), `name` (human-readable, not unique: identical monitors share it), `kind` (Display / Keyboard / ExternalDisplay), `brightness`, `maxBrightness`, `percentage`; `setBrightness()`, `setPercentage()` (sysfs devices write fire-and-forget via logind, and external displays route to the DDC worker). |
| `BrightnessHost`        | Enumerates the brightness devices under a sysfs root, resolves the logind session for sysfs writes, and (when built with libddcutil) enumerates external monitors over DDC/CI. `deviceCount`, `deviceAt()`, `devices()`; `deviceAdded` / `deviceRemoved`. |
| `BrightnessDeviceModel` | `QAbstractListModel` over the host's devices, tracking async `deviceAdded` / `deviceRemoved` (external displays arrive after enumeration). Roles: `device`, `id`, `name`, `kind`, `brightness`, `maxBrightness`, `percentage`. |

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
  `brightness` attribute (`max_brightness` for the range). Writes go through
  logind `Session.SetBrightness(subsystem, name, value)` with the matching
  subsystem (`backlight` for display, `leds` for keyboard), so the lib needs
  no root and no udev rules. With no logind session bound the write no-ops and
  the lib stays read-only.
- **No optimistic writes.** `setBrightness` / `setPercentage` issue the logind
  call and let the file watcher pick up the applied value. The cached
  `brightness` (and its NOTIFY) moves only once sysfs actually changes. External
  displays work the same way. The write routes to the DDC worker and the cached
  value moves only on the worker's read-back.
- **External displays (DDC/CI), optional.** When built against `libddcutil`
  (ddcutil 2.x, detected via pkg-config), the host also enumerates external
  monitors over DDC/CI on I2C (VCP feature `0x10`) and surfaces them as
  `ExternalDisplay` devices. The dependency is compile-time optional: without it
  the library builds identically and simply surfaces no external displays, and
  the sysfs + logind path is unchanged. libddcutil I2C calls are blocking and
  slow, so enumeration and get/set run on a dedicated worker `QThread` and post
  back via queued signals. DDC/CI has no change notification and logind does not
  mediate it, so external displays need `/dev/i2c-*` access (the `i2c` group + a
  udev rule) and refresh by polling rather than a watcher. Without I2C access
  the lib degrades to no external displays.
- **Scope is brightness, not LEDs.** Display backlights plus the
  `kbd_backlight` entries under `/sys/class/leds`. Every other LED (indicator,
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
- A running `org.freedesktop.login1` (logind) on the system bus for the sysfs
  write path (the lib loads read-only inert without it).
- `PhosphorDBus` (private link; the logind `SetBrightness` call).
- Qt6 ≥ 6.6 (Core, Qml, DBus). No QtGui.
- `libddcutil` (ddcutil 2.x), optional: enables external-monitor (DDC/CI)
  brightness when present. The build is unaffected when it is absent.

## Status

Shipped. The sysfs read path with live `QFileSystemWatcher` updates
(`BrightnessDevice`), the logind write path (`setBrightness` / `setPercentage`
via `Session.SetBrightness`), enumeration of display + `kbd_backlight` devices
with the indicator-LED exclusion (`BrightnessHost`), the `QAbstractListModel`
(`BrightnessDeviceModel`), the optional external-monitor DDC/CI source
(`ExternalDisplay` devices via a `libddcutil` worker thread), and the
`phosphorctl`-style CLI demo (`examples/phosphor-service-brightness-cli`: list,
get, set) covering the core capabilities (list devices, get/set brightness). The smoke harness pins enumeration, the `kbd_backlight` filter,
percentage math, divide-by-zero safety, live watcher updates, model role names
plus its async add / remove and host-destroyed reset, the logind write path for
both the `backlight` and `leds` subsystems, and the `ExternalDisplay`
route/clamp/read-back path deterministically against a fake sysfs tree, a fake
logind over a session-bus loopback, and a recording setter (no I2C), no root or
daemon required. The live DDC/CI worker is hardware-bound and validated through
the CLI against real monitors.
