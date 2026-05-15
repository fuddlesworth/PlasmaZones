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

The API mirrors Quickshell where it matters (`PanelWindow`, `Variants`,
`LazyLoader`, `PersistentProperties`, `Process`, `FileView`) so existing
configs port with minimal rework.

## Key types

| Type | Purpose |
|------|---------|
| `ShellEngine`            | Top-level lifecycle: QML engine, config discovery, reload. |
| `ShellLoader`            | Discovers and loads the user's QML config from XDG paths. |
| `PanelWindow`            | Layer-shell-backed panel window with edge anchoring and exclusive zone. |
| `PopupWindow`            | Top-layer popup with optional keyboard grab and parent anchoring. |
| `FloatingWindow`         | XDG-toplevel window for non-shell auxiliary UI. |
| `Variants`               | Lazy per-screen instantiation of declarative content. |
| `LazyLoader`             | Defer-instantiation helper for expensive QML trees. |
| `PersistentProperties`   | QML-friendly persistence of property values across launches. |
| `Process`                | Sandboxed subprocess runner exposed to QML. |
| `FileView`               | Watched-file reader for QML config-driven panels. |
| `Environment`            | Cross-platform env-var, locale, and runtime-dir helpers. |
| `ShellGlobal`            | QML singleton with shell-wide config and runtime state. |
| `ScreenModel`            | Reactive list of physical screens for per-screen `Variants`. |
| `Toplevels`              | `ext-foreign-toplevel-list-v1` consumer (window list for taskbars). |

## Dependencies

- `Qt6::Core`, `Qt6::Gui`, `Qt6::Quick`, `Qt6::Qml`
- [`phosphor-layer`](../phosphor-layer/README.md), [`phosphor-rendering`](../phosphor-rendering/README.md), [`phosphor-shaders`](../phosphor-shaders/README.md), [`phosphor-wayland`](../phosphor-wayland/README.md)

The shell binary is gated behind the `BUILD_PHOSPHOR_SHELL` CMake option
(default OFF). The example under `examples/phosphor-shell/` is the
working reference for consumers.

## See also

- [`phosphor-services`](../phosphor-services/README.md) — system tray and dbusmenu.
- [`phosphor-layer`](../phosphor-layer/README.md) — Role vocabulary the window types compose from.
- [`phosphor-shell-patterns`](../phosphor-shell-patterns/README.md) — named Role recipes the panels use.
