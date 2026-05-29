<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-sni

System-tray host implementing `org.kde.StatusNotifierItem` /
`org.kde.StatusNotifierWatcher` plus the `com.canonical.dbusmenu`
context-menu model.

## Responsibility

Registers the well-known `org.kde.StatusNotifierWatcher` service on the session bus, advertises hosts via `RegisterStatusNotifierHost`, watches `NameOwnerChanged` for tray-item appearance / disappearance, and exposes each live item plus its context menu as a Qt model that QML can bind to.

No UI; the shell decides how the tray slot is rendered (capsule, dock, panel widget, free-floating overlay).

## Key types

| Type                        | Role                                                                                                     |
|-----------------------------|----------------------------------------------------------------------------------------------------------|
| `StatusNotifierHost`        | Per-shell host. Registers the watcher service if absent, declares itself as a host, owns the live items. |
| `StatusNotifierWatcher`     | Implements `org.kde.StatusNotifierWatcher` so apps and other hosts can discover the host.                |
| `StatusNotifierItem`        | Live proxy for one tray item. Surfaces icon (`QImage` + URL forms), tooltip, status, menu path.          |
| `StatusNotifierItemModel`   | `QAbstractListModel` over the host's items. Roles include id/title/status/iconUrl/menuPath etc.          |
| `DBusMenuModel`             | `QAbstractItemModel` over `com.canonical.dbusmenu`. Hierarchical; supports activation + ToolTip lookups. |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceSni/QmlRegistration.h>
#include <PhosphorServiceIconTheme/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceSni::registerQmlTypes();
    PhosphorServiceIconTheme::registerQmlTypes();
    QQmlApplicationEngine engine;
    PhosphorServiceIconTheme::installImageProvider(&engine);
    // ... load shell.qml
}
```

QML consumer:

```qml
import Phosphor.Service.Sni 1.0

StatusNotifierHost { id: tray }
StatusNotifierItemModel { id: items; host: tray }

Repeater {
    model: items
    delegate: Image {
        source: iconUrl       // already an image://phosphor-service-icontheme/ URL
        sourceSize: Qt.size(20, 20)
    }
}
```

## Design notes

- **Watcher election.** The watcher service has a well-known bus name (`org.kde.StatusNotifierWatcher`). Multiple shells racing for it is normal during compositor restart; whichever wins the name owns the watcher, the others observe `NameOwnerChanged` and reuse the winner. `StatusNotifierHost` handles this transparently — if you write two shells, both still see the tray.
- **Icon publishing.** Items can ship an `IconName` (theme lookup via `phosphor-service-icontheme`), an `IconThemePath` (custom dir for non-system icons), and / or inline `IconPixmap` blobs. The model collapses all three into a single URL of the form `image://phosphor-service-icontheme/<service|path[#variant]>?v=<cacheKey>` per item. The `?v=` cache-key bust forces QML's `Image` element to re-fetch when the underlying data changes (it only diffs URL strings).
- **`com.canonical.dbusmenu`.** Menus are hierarchical and lazy — `AboutToShow` populates the root layout, `Event` fires activation. `DBusMenuModel` exposes a `QAbstractItemModel` shaped for QML's `Instantiator` / `TreeView`, and routes activation back to the item's process via the same bus.
- **dbustypes forced-include.** The generated D-Bus interface headers reference custom marshalled types (`DBusImageList`, `DBusToolTip`, `DBusMenuLayoutItem`) without `#include`ing `dbustypes.h`. The build forces the include into every TU (`-include src/dbustypes.h`) so AUTOMOC produces valid output for the generated headers in isolation. Unity build is disabled for the same reason.

## Dependencies

- Qt6 ≥ 6.6 (Core, Gui, Qml, Quick, DBus)
- `phosphor-service-icontheme` (XDG icon-theme resolver + Qt image provider)
- A running session bus

## Status

Phase 2.0 extraction from the original `phosphor-services` umbrella, and the last of the four. With this extraction the umbrella is deleted; no backwards-compat shim. Namespace `PhosphorServices::StatusNotifier*` → `PhosphorServiceSni::StatusNotifier*`, QML module `Phosphor.Services` → `Phosphor.Service.Sni`.
