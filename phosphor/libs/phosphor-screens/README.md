<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-screens

> Physical and virtual screen topology, panel reservations, and the canonical
> `org.plasmazones.Screen` D-Bus surface.

## Responsibility

Owns the mapping from "here's a cursor position" to "here's the screen ID
you should route the next event to." Tracks physical screens as reported
by `QScreen`, user-defined virtual sub-regions within each physical
screen, and panel reservations from taskbars, docks, and status bars that
eat part of the usable geometry. Exposes all of it on the canonical
`org.plasmazones.Screen` D-Bus interface so downstream consumers stay
compositor-agnostic.

## Targets

The library is split so the screen-topology core does not drag QtDBus into
pure-compute consumers:

| CMake target | Links | Linked by |
|--------------|-------|-----------|
| `PhosphorScreens::Core`            | `Qt6::Core` + `Qt6::Gui` | Domain libraries (zones, snap/tile engines, placement) that need POD topology types + identity but never touch the bus or the live manager. |
| `PhosphorScreens::Runtime`         | `::Core` + `Qt6::Core` + `Qt6::Gui` (+ `PhosphorWayland` privately) | Consumers that need the live `ScreenManager` (physical-screen tracking via Wayland). |
| `PhosphorScreens::PhosphorScreens` | `::Runtime` + `Qt6::DBus` + `PhosphorProtocol::Types` | The daemon and anything that needs the D-Bus surface (`DBusScreenAdaptor`, `PlasmaPanelSource`, `ScreenResolver`). |

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorScreens::ScreenManager`      | Physical + virtual topology state with change signals as screens come and go. |
| `PhosphorScreens::ScreenResolver`     | Point-to-screen lookup that accepts an optional D-Bus endpoint override. |
| `PhosphorScreens::IPanelSource`       | Pluggable panel-reservation source per desktop (Plasma, GNOME, wlr). |
| `PhosphorScreens::PlasmaPanelSource`  | Bundled `IPanelSource` for `org.kde.plasmashell` reservations. |
| `PhosphorScreens::VirtualScreenDef`   | One rectangular sub-region of a physical screen with its own screen ID. |
| `PhosphorScreens::VirtualScreenSwapper` | D-Bus-addressable directional virtual-screen swaps (`left`, `right`, `up`, `down`). |
| `PhosphorScreens::DBusScreenAdaptor`  | Canonical `org.plasmazones.Screen` D-Bus surface. |
| `PhosphorScreens::IConfigStore`       | Persisted virtual-screen configuration. `InMemoryConfigStore` is the default for tests. |

## Design notes

- **Virtual screens are first-class.** Each virtual screen gets its own
  screen ID, layout assignments, autotile state, and overlay windows.
  Everything downstream treats them exactly like physical screens.
- **Panel source is pluggable.** Plasma exposes reservations via
  `org.kde.plasmashell`, GNOME via `org.gnome.Mutter`, and sway and
  Hyprland via wlr-foreign-toplevel. The manager core stays
  compositor-agnostic by delegating to an `IPanelSource` owned by the
  consumer.
- **Direction tokens match the wire format.** `Direction::Left` /
  `Right` / `Up` / `Down` are the same lower-case ASCII strings the
  D-Bus `swapVirtualScreenInDirection` method accepts, so adaptors can
  pass user strings through verbatim.
- **The D-Bus surface is a separate target.** `DBusScreenAdaptor`, the
  Plasma panel source, and the resolver live in `PhosphorScreens`. The
  live `ScreenManager` lives in `PhosphorScreens::Runtime`, and the POD
  topology types live in `PhosphorScreens::Core`. A snap or tiling
  engine links `::Core` and never pulls QtDBus in to reason about
  screens.

## Dependencies

- `PhosphorScreens::Core` — `QtCore`, `QtGui`
- `PhosphorScreens::Runtime` — additionally `phosphor-wayland` (private)
  for the live `ScreenManager`
- `PhosphorScreens` — additionally `QtDBus` and `PhosphorProtocol::Types`
  (for the `org.plasmazones.Screen` service constants)
- [`phosphor-identity`](../phosphor-identity/README.md) — `VirtualScreenId` format helpers

## See also

- [`phosphor-identity`](../phosphor-identity/README.md) — where the screen-id wire format lives.
- [`phosphor-protocol`](../phosphor-protocol/README.md) — `org.plasmazones.Screen` interface name comes from `ServiceConstants`.
