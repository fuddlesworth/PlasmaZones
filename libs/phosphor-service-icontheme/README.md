<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-icontheme

XDG Icon Theme Specification 0.13 lookup + the Qt image provider that lets QML consume the resulting `QImage` payloads through a `QUrl` source.

## Responsibility

- **`IconThemeResolver`** — spec-compliant theme walk. Detects the active theme from Qt + GTK env hints, parses each theme's `index.theme`, follows `Inherits=` chains, and falls back to Hicolor / direct filesystem search for absolute paths. `iconForName(name, size, scale, extraThemeDir)` returns the best-match `QImage` by the spec's distance algorithm.
- **`IconImageProvider`** — Qt image provider mounted at `image://phosphor-service-icontheme/`. The publisher (e.g. a tray-item model holding `QImage` payloads from DBus) calls `IconImageProvider::setImage(id, image)`; the consumer binds `Image.source` to a URL of the form `image://phosphor-service-icontheme/<id>?v=<cacheKey>`; Qt routes that back through `requestImage()` which reads the QImage out of the thread-safe static registry. The `?v=` suffix exists only to force QML's `Image` element to re-fetch when the underlying QImage data changes (QML's `Image` only reloads when the URL string differs).

## Key types

| Type                | Role                                                                                                |
|---------------------|-----------------------------------------------------------------------------------------------------|
| `IconThemeResolver` | Singleton XDG icon resolver. `iconForName` + `decodePixmaps` static for raw IconPixmap byte blobs.  |
| `IconImageProvider` | `QQuickImageProvider` with a process-global registry; `setImage` / `clearImage` from publishers.    |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceIconTheme/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceIconTheme::registerQmlTypes();          // exposes IconThemeResolver singleton
    QQmlApplicationEngine engine;
    PhosphorServiceIconTheme::installImageProvider(&engine); // mounts image://phosphor-service-icontheme/
    // ... load shell.qml
}
```

QML consumer:

```qml
import Phosphor.Service.IconTheme 1.0

Image {
    source: IconThemeResolver.iconForName("audio-volume-high", 24, 1) // returns a QImage path or empty
    sourceSize: Qt.size(24, 24)
}
```

Publisher (typically internal to a service that holds raw `QImage` payloads):

```cpp
#include <PhosphorServiceIconTheme/IconImageProvider.h>

PhosphorServiceIconTheme::IconImageProvider::setImage(itemId, qimage);
// then expose: image://phosphor-service-icontheme/<itemId>?v=<qimage.cacheKey()>
```

## Design notes

- **Static registry, not per-engine.** Shells may construct multiple `QQmlEngine`s across reload cycles; the icon payload is valid across all of them. Coarse single-mutex locking is fine at human update rates.
- **URL host segment stability.** The `imageProviderUrlHost()` accessor exists so publishers don't hard-code the host string; a rename here breaks the link rather than failing silently at runtime.
- **`?v=` cache busting.** `Image` re-fetches only on URL change. The publisher appends `?v=cacheKey()` so a fresh `setImage` for the same id forces a rebind. `requestImage` strips the query before lookup.

## Dependencies

- Qt6 ≥ 6.6 (Core, Gui, Qml, Quick)

## Status

Phase 2.0 extraction from the original `phosphor-services` umbrella. URL host renamed from `phosphor-services` to `phosphor-service-icontheme` for consistency with the new library name; consumers of the old URL fail loudly rather than silently fall back. Namespace `PhosphorServices::IconThemeResolver` → `PhosphorServiceIconTheme::IconThemeResolver`.
