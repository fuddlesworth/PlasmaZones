<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-shell

> Quickshell-style declarative QML framework for layer-shell desktop
> shells. Provides the shell lifecycle (XDG config discovery, QML engine
> management) and the user-facing QML window types that create
> layer-shell surfaces.

## Responsibility

The infrastructure layer between `phosphor-wayland` / `phosphor-layer`
(which know how to make a layer-shell surface) and the consumer QML
config that describes what the shell should look like. A shell binary
constructs a `ShellEngine`, points it at an XDG-located config tree,
and the engine instantiates the user's QML which declares `PanelWindow`,
`PopupWindow`, and friends. Each window type creates the appropriate
layer-shell surface through `phosphor-layer` underneath, so the QML
author writes panels without touching wlr-protocol primitives.

`ShellEngine` also drives hot reload: it watches the config tree and the
screen topology, rebuilds the QML engine on change, and saves/restores
`PersistentProperties` state across the reload.

The API mirrors Quickshell where it matters (`PanelWindow`, `Variants`,
`LazyLoader`, `PersistentProperties`, `Process`, `FileView`) so existing
configs port with minimal rework.

## Key types

| Type | Purpose |
|------|---------|
| `ShellEngine`            | Top-level lifecycle: QML engine, config discovery, hot reload. |
| `ShellLoader`            | Discovers and loads the user's QML config from XDG paths. |
| `PanelWindow`            | Layer-shell-backed panel window with edge anchoring and exclusive zone. |
| `PopupWindow`            | xdg-popup positioned against an anchor item, with in-place reposition. |
| `FloatingWindow`         | XDG-toplevel window for non-shell auxiliary UI. |
| `Variants`               | Lazy per-screen instantiation of declarative content. |
| `LazyLoader`             | Defer-instantiation helper for expensive QML trees. |
| `PersistentProperties`   | QML-friendly persistence of property values across launches and hot reloads. |
| `Process`                | Sandboxed subprocess runner exposed to QML. |
| `FileView`               | Watched-file reader for QML config-driven panels (`/proc`, `/sys`, config files). |
| `SystemClock`            | Timer-driven clock (hours/minutes/seconds/date) with configurable tick precision. |
| `WallpaperService`       | Decodes the desktop wallpaper off the GUI thread. Reached from QML via `ShellGlobal.wallpaper`. |
| `Environment`            | Cross-platform env-var, locale, and runtime-dir helpers. |
| `ShellGlobal`            | QML context object (`PhosphorShell`) with shell-wide config and runtime state. |
| `ScreenModel`            | Reactive list of physical screens for per-screen `Variants`. |
| `Toplevels`              | `ext-foreign-toplevel-list-v1` consumer (window list for taskbars). |

## QML module

`ShellEngine` registers the `Phosphor.Shell` import. Alongside the types
above it re-exports a few from sibling libraries, so shell QML needs a
single import:

- `ShaderBackground`: animated shader surface (from `phosphor-rendering`).
- `IdleInhibitor`: surface-bound idle inhibition (from `phosphor-wayland`).
  Session-wide idle detection lives in `Phosphor.Service.Idle`'s `IdleService`.
- `ForeignToplevel`: one entry of the `Toplevels` window list
  (uncreatable, vended by `Toplevels`).

## Dependencies

- `Qt6::Core`, `Qt6::Gui`, `Qt6::Quick`, `Qt6::Qml`
- [`phosphor-layer`](../phosphor-layer/README.md), [`phosphor-rendering`](../phosphor-rendering/README.md), [`phosphor-shaders`](../phosphor-shaders/README.md), [`phosphor-wayland`](../phosphor-wayland/README.md)

The shell binary is gated behind the `BUILD_PHOSPHOR_SHELL` CMake option
(default OFF). The example under `examples/phosphor-shell/` is the
working reference for consumers.

## See also

- [`phosphor-service-sni`](../phosphor-service-sni/README.md): StatusNotifierItem tray + dbusmenu.
- [`phosphor-service-mpris`](../phosphor-service-mpris/README.md): MPRIS2 media-player discovery + control.
- [`phosphor-service-upower`](../phosphor-service-upower/README.md): battery + power-supply readouts.
- [`phosphor-service-icontheme`](../phosphor-service-icontheme/README.md): XDG icon-theme resolver + Qt image provider used by the tray.
- [`phosphor-layer`](../phosphor-layer/README.md): Role vocabulary the window types compose from.
- [`phosphor-shell-patterns`](../phosphor-shell-patterns/README.md): named Role recipes the panels use.
