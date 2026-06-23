<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-sni

System-tray host implementing `org.kde.StatusNotifierItem` /
`org.kde.StatusNotifierWatcher` plus the `com.canonical.dbusmenu`
context-menu model.

## Responsibility

Registers the well-known `org.kde.StatusNotifierWatcher` service on the session bus, advertises hosts via `RegisterStatusNotifierHost`, watches `NameOwnerChanged` for tray-item appearance / disappearance, and exposes each live item plus its context menu as a Qt model that QML can bind to.

There is no UI. The shell decides how the tray slot is rendered (capsule, dock, panel widget, free-floating overlay).

## Key types

| Type                        | Role                                                                                                     |
|-----------------------------|----------------------------------------------------------------------------------------------------------|
| `StatusNotifierHost`        | Per-shell host. Registers the watcher service if absent, declares itself as a host, owns the live items. |
| `StatusNotifierWatcher`     | Implements `org.kde.StatusNotifierWatcher` so apps and other hosts can discover the host.                |
| `StatusNotifierItem`        | Live proxy for one tray item. Surfaces icon (`QImage`), tooltip, status, menu path. URL forms live on `StatusNotifierItemModel` as role data. |
| `StatusNotifierItemModel`   | `QAbstractListModel` over the host's items. Roles include id/title/status/iconUrl/menuPath etc.          |
| `DBusMenuModel`             | `QAbstractListModel` exposing one level of a `com.canonical.dbusmenu` tree. Cascaded popups bind a fresh model per level (the QML side prefers this over a hierarchical model). |

## Typical use

C++ shell composition root. The icon image provider must be mounted on every `QQmlEngine` the shell constructs. If the shell hot-reloads (a fresh engine per reload) the per-engine hook is what keeps the provider mounted after the rebuild:

```cpp
#include <PhosphorServiceSni/QmlRegistration.h>
#include <PhosphorServiceIconTheme/QmlRegistration.h>
#include <PhosphorShell/ShellEngine.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceSni::registerQmlTypes();
    PhosphorServiceIconTheme::registerQmlTypes();

    // For a one-shot QQmlApplicationEngine, install once after
    // construction:
    //   QQmlApplicationEngine engine;
    //   PhosphorServiceIconTheme::installImageProvider(&engine);
    //
    // For a hot-reloading shell that builds a fresh QQmlEngine per
    // reload, register the install as an engine hook so it fires on
    // every rebuild:
    PhosphorShell::ShellEngine engine{...};
    engine.addEngineHook([](QQmlEngine* qmlEngine) {
        PhosphorServiceIconTheme::installImageProvider(qmlEngine);
    });
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

- **Watcher election.** The watcher service has a well-known bus name (`org.kde.StatusNotifierWatcher`). Multiple shells racing for it is normal during compositor restart. Whichever wins the name owns the watcher, and the others observe `NameOwnerChanged` and reuse the winner. `StatusNotifierHost` handles this transparently, so if you write two shells, both still see the tray.
- **Icon publishing.** Items can ship an `IconName` (theme lookup via `phosphor-service-icontheme`), an `IconThemePath` (custom dir for non-system icons), and / or inline `IconPixmap` blobs. The model collapses all three into a single URL of the form `image://phosphor-service-icontheme/<service|path[#variant]>?v=<cacheKey>` per item. The `?v=` cache-key bust forces QML's `Image` element to re-fetch when the underlying data changes (it only diffs URL strings).
- **`com.canonical.dbusmenu`.** Menus are hierarchical and lazy: `AboutToShow` populates a level on demand, `Event` fires activation. `DBusMenuModel` is a flat `QAbstractListModel` over one level of the tree, and cascaded popups bind a fresh model rooted at the parent row's id. The QML side renders submenus as cascading popups, which fits a per-level flat model better than a tree-shaped one.
- **dbustypes forced-include.** The generated D-Bus interface headers reference custom marshalled types (`DBusImageList`, `DBusToolTip`, `DBusMenuLayoutItem`) without `#include`ing `dbustypes.h`. The build forces the include into every TU (`-include src/dbustypes.h`) so AUTOMOC produces valid output for the generated headers in isolation. Unity build is disabled for the same reason. The `-include` flag is GCC/Clang-only, and the project targets Linux/Qt6 so MSVC support is out of scope.

## Dependencies

- Qt6 ≥ 6.6 (Core, Gui, Qml, DBus)
- `phosphor-service-icontheme` (XDG icon-theme resolver + Qt image provider)
- A running session bus

## Status

Shipped. Extracted from the original `phosphor-services` umbrella as one of four per-domain siblings and the last of the umbrella tenants. The umbrella is gone, no backwards-compat shim (per `feedback_no_legacy_shims`). Namespace `PhosphorServices::StatusNotifier*` → `PhosphorServiceSni::StatusNotifier*`, QML module `Phosphor.Services` → `Phosphor.Service.Sni`.
