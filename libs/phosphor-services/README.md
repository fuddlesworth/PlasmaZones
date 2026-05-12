<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-services

> D-Bus and platform-integration primitives for Phosphor-based desktop
> shells. The library has no UI of its own; shells and downstream apps
> consume the C++/QML APIs and render the results however they like.

## Responsibility

A grab-bag of small D-Bus and spec-driven services every desktop shell
needs, exposed under a single `PhosphorServices::` namespace so a shell
can pull them in piecewise without growing a giant dependency on a
single integration crate.

First tenant: **StatusNotifierItem** (system tray) host + watcher with
full XDG icon-theme spec lookup and `com.canonical.dbusmenu` support. A
shell registers a `StatusNotifierHost` instance, points its QML tray
view at `StatusNotifierItemModel`, and the library handles the watcher
registration, item discovery, icon resolution, and dbusmenu activation
end-to-end.

Future siblings live behind the same namespace as they're added:
`org.freedesktop.Notifications`, MPRIS, UPower, NetworkManager, logind,
`ext-session-lock-v1`, `ext-idle-notify-v1`.

## Key types

| Type | Purpose |
|------|---------|
| `StatusNotifierHost`       | Per-shell host that registers as a watcher and tracks live items. |
| `StatusNotifierWatcher`    | Implements `org.kde.StatusNotifierWatcher` so apps discover the host. |
| `StatusNotifierItem`       | Live proxy for one tray item; surfaces icon, tooltip, status, and menu. |
| `StatusNotifierItemModel`  | `QAbstractListModel` over the host's items for QML binding. |
| `DBusMenuModel`            | `QAbstractItemModel` over `com.canonical.dbusmenu` for context-menu rendering. |
| `IconThemeResolver`        | XDG icon-theme lookup with size and theme fallbacks. |
| `QmlRegistration`          | Registers the QML-exposed types under the `PhosphorServices` import URI. |

## Dependencies

- `Qt6::Core`, `Qt6::Gui`, `Qt6::Qml`, `Qt6::Quick`, `Qt6::DBus`

## See also

- [`phosphor-shell`](../phosphor-shell/README.md) — QML shell infrastructure that consumes these services.
- [`phosphor-layer`](../phosphor-layer/README.md) — layer-shell primitives the shell surfaces are built on.
