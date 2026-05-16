<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-services

> D-Bus and platform-integration primitives for Phosphor-based desktop
> shells. The library has no UI of its own; shells and downstream apps
> consume the C++/QML APIs and render the results however they like.

## Responsibility

The spec-driven D-Bus services a desktop shell needs, each under the
`PhosphorServices::` namespace and the `Phosphor.Services` QML import so
a shell can pull in only what it uses. Three service families ship
today:

- **System tray** — `org.kde.StatusNotifierItem` host + watcher with
  full XDG icon-theme lookup and `com.canonical.dbusmenu` context
  menus. A shell registers a `StatusNotifierHost`, binds its QML tray
  view to `StatusNotifierItemModel`, and the library handles watcher
  registration, item discovery, icon resolution, and menu activation.
- **Media control** — MPRIS2 (`org.mpris.MediaPlayer2`) player
  discovery and control. `MprisHost` watches the session bus for
  players appearing and disappearing; each `MprisPlayer` exposes
  playback state, track metadata, position, volume, and transport
  controls.
- **Power** — UPower (`org.freedesktop.UPower`) battery and
  power-supply state. `UPowerHost` exposes the aggregate display
  device, the full device list, and the on-battery flag; each
  `UPowerDevice` reports percentage, charge state, time estimates, and
  health.

All three are hand-rolled `QDBusMessage` async-call clients (no
generated proxies): property fetches batch through `GetAll` and never
block the GUI thread.

## Key types

### System tray

| Type | Purpose |
|------|---------|
| `StatusNotifierHost`       | Per-shell host that registers as a watcher and tracks live items. |
| `StatusNotifierWatcher`    | Implements `org.kde.StatusNotifierWatcher` so apps discover the host. |
| `StatusNotifierItem`       | Live proxy for one tray item; surfaces icon, tooltip, status, and menu. |
| `StatusNotifierItemModel`  | `QAbstractListModel` over the host's items for QML binding. |
| `DBusMenuModel`            | `QAbstractItemModel` over `com.canonical.dbusmenu` for context-menu rendering. |
| `IconThemeResolver`        | XDG icon-theme lookup with size and theme fallbacks (QML singleton). |

### Media (MPRIS2)

| Type | Purpose |
|------|---------|
| `MprisHost`        | Watches the session bus for `org.mpris.MediaPlayer2.*` players. |
| `MprisPlayer`      | One player: playback state, metadata, position, volume, transport controls. Owned by `MprisHost`. |
| `MprisPlayerModel` | `QAbstractListModel` over `MprisHost` for QML binding. |

### Power (UPower)

| Type | Purpose |
|------|---------|
| `UPowerHost`        | Connects to UPower on the system bus; exposes the display device, device list, and on-battery flag. |
| `UPowerDevice`      | One power device: percentage, state, type, time-to-empty/full, energy, health. Owned by `UPowerHost`. |
| `UPowerDeviceModel` | `QAbstractListModel` over `UPowerHost` for QML binding. |

## QML registration

`registerQmlTypes()` registers the QML-exposed types under the
`Phosphor.Services` import URI; the host application must call it once
before loading QML. `MprisPlayer` and `UPowerDevice` are registered
uncreatable — they are vended by their hosts and models, never
constructed from QML.

## Dependencies

- `Qt6::Core`, `Qt6::Gui`, `Qt6::Qml`, `Qt6::Quick`, `Qt6::DBus`

## See also

- [`phosphor-shell`](../phosphor-shell/README.md) — QML shell infrastructure that consumes these services.
- [`phosphor-layer`](../phosphor-layer/README.md) — layer-shell primitives the shell surfaces are built on.
