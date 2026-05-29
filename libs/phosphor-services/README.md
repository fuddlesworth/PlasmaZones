<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-services

> D-Bus and platform-integration primitives for Phosphor-based desktop
> shells. The library has no UI of its own; shells and downstream apps
> consume the C++/QML APIs and render the results however they like.

## Responsibility

The spec-driven D-Bus services a desktop shell needs, each under the
`PhosphorServices::` namespace and the `Phosphor.Services` QML import so
a shell can pull in only what it uses. One service family ships here
today; UPower, MPRIS, and the icon-theme resolver / image provider
have been extracted into sibling libraries (see [Phase 2.0 extraction
note](#phase-20-extraction)).

- **System tray** — `org.kde.StatusNotifierItem` host + watcher with
  `com.canonical.dbusmenu` context menus. A shell registers a
  `StatusNotifierHost`, binds its QML tray view to
  `StatusNotifierItemModel`, and the library handles watcher
  registration, item discovery, icon URL publishing via
  `phosphor-service-icontheme`, and menu activation.

Hand-rolled `QDBusMessage` async-call clients (no generated proxies):
property fetches batch through `GetAll` and never block the GUI
thread.

## Key types

### System tray

| Type | Purpose |
|------|---------|
| `StatusNotifierHost`       | Per-shell host that registers as a watcher and tracks live items. |
| `StatusNotifierWatcher`    | Implements `org.kde.StatusNotifierWatcher` so apps discover the host. |
| `StatusNotifierItem`       | Live proxy for one tray item; surfaces icon, tooltip, status, and menu. |
| `StatusNotifierItemModel`  | `QAbstractListModel` over the host's items for QML binding. |
| `DBusMenuModel`            | `QAbstractItemModel` over `com.canonical.dbusmenu` for context-menu rendering. |

### Media (MPRIS2)

Moved to [`phosphor-service-mpris`](../phosphor-service-mpris/README.md). Consumers import `Phosphor.Service.Mpris 1.0` and link `PhosphorServiceMpris::PhosphorServiceMpris` separately.

### Power (UPower)

Moved to [`phosphor-service-upower`](../phosphor-service-upower/README.md). Consumers import `Phosphor.Service.UPower 1.0` and link `PhosphorServiceUPower::PhosphorServiceUPower` separately.

## QML registration

`registerQmlTypes()` registers the QML-exposed types under the
`Phosphor.Services` import URI; the host application must call it once
before loading QML. `StatusNotifierItem` is registered uncreatable —
it is vended by `StatusNotifierHost` / `StatusNotifierItemModel` and
never constructed from QML.

The icon image provider that used to live here has moved to
`phosphor-service-icontheme`; consumers call
`PhosphorServiceIconTheme::installImageProvider(engine)` directly. SNI
publishes URLs through `image://phosphor-service-icontheme/<id>?v=<cacheKey>`
and uses `IconThemeResolver` from the same library for theme walks.

## Phase 2.0 extraction

The umbrella is being dissolved per Phase 2.0 of
`docs/phosphor-shell-design/04-implementation-plan.md`. UPower, MPRIS,
and the icon-theme resolver / image provider have already moved out;
StatusNotifierItem is the last tenant.

## Dependencies

- `Qt6::Core`, `Qt6::Gui`, `Qt6::Qml`, `Qt6::Quick`, `Qt6::DBus`

## See also

- [`phosphor-shell`](../phosphor-shell/README.md) — QML shell infrastructure that consumes these services.
- [`phosphor-layer`](../phosphor-layer/README.md) — layer-shell primitives the shell surfaces are built on.
