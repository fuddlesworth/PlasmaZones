<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-icontheme

XDG Icon Theme Specification 0.13 lookup + the Qt image provider that lets QML consume the resulting `QImage` payloads through a `QUrl` source.

## Responsibility

- **`IconThemeResolver`**: spec-compliant theme walk. Detects the active theme via `QIcon::themeName()` (which the Qt platform plugin sources from Plasma, GTK, or xsettings as available), parses each theme's `index.theme`, follows `Inherits=` chains, and falls back to Hicolor or direct filesystem search for absolute paths. `iconForName(name, size, scale, extraThemeDir)` returns the best-match `QImage` by the spec's distance algorithm.
- **`IconImageProvider`**: Qt image provider mounted at `image://phosphor-service-icontheme/`. The publisher (e.g. a tray-item model holding `QImage` payloads from D-Bus) calls `IconImageProvider::setImage(id, image)`. The consumer binds `Image.source` to a URL of the form `image://phosphor-service-icontheme/<id>?v=<cacheKey>`, and Qt routes that back through `requestImage()` which reads the QImage out of the thread-safe static registry. The `?v=` suffix exists only to force QML's `Image` element to re-fetch when the underlying QImage data changes (QML's `Image` only reloads when the URL string differs).

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

QML consumer (resolve an XDG icon, then route the resulting QImage through the image provider so QML's `Image.source` can consume it as a URL):

```qml
import Phosphor.Service.IconTheme 1.0
import QtQuick

Image {
    // IconThemeResolver.iconForName returns a QImage, which Image.source
    // (a QUrl) cannot bind to directly. The C++ side must call
    // IconImageProvider::setImage(id, image) and then expose the URL
    // form below; the snippet here illustrates the QML end of that
    // contract.
    property string trayKey: "tray-audio-volume-high"
    source: "image://phosphor-service-icontheme/" + trayKey + "?v=" + cacheVersion
    property int cacheVersion: 0
    sourceSize: Qt.size(24, 24)
}
```

Publisher (typically internal to a service that holds raw `QImage` payloads, e.g. phosphor-service-sni's tray-item model):

```cpp
#include <PhosphorServiceIconTheme/IconImageProvider.h>
#include <PhosphorServiceIconTheme/QmlRegistration.h>

const QString trayKey = QStringLiteral("tray-audio-volume-high");
PhosphorServiceIconTheme::IconImageProvider::setImage(trayKey, qimage);
// then expose the QML-bindable URL:
const QString url = QStringLiteral("image://")
    + QString::fromLatin1(PhosphorServiceIconTheme::imageProviderUrlHost())
    + QLatin1Char('/') + trayKey + QStringLiteral("?v=")
    + QString::number(qimage.cacheKey());
```

## Design notes

- **Static registry, not per-engine.** Shells may construct multiple `QQmlEngine`s across reload cycles. The icon payload is valid across all of them. Coarse single-mutex locking is fine at human update rates.
- **URL host segment stability.** The `imageProviderUrlHost()` accessor exists so publishers don't hard-code the host string. A rename here breaks the link rather than failing silently at runtime.
- **`?v=` cache busting.** `Image` re-fetches only on URL change. The publisher appends `?v=cacheKey()` so a fresh `setImage` for the same id forces a rebind. `requestImage` strips the query before lookup.

## Dependencies

- Qt6 ≥ 6.6 (Core, Gui, Qml, Quick)

## Status

Shipped. Extracted from the original `phosphor-services` umbrella as one of four per-domain siblings. The umbrella is gone, with no backwards-compat shim (per `feedback_no_legacy_shims`). The URL host was renamed from `phosphor-services` to `phosphor-service-icontheme` for consistency with the new library name, and consumers of the old URL fail loudly rather than silently fall back. Namespace `PhosphorServices::IconThemeResolver` becomes `PhosphorServiceIconTheme::IconThemeResolver`, QML module `Phosphor.Services` becomes `Phosphor.Service.IconTheme`.

## Current consumers

- `phosphor-service-sni` calls `IconThemeResolver::instance()->iconForName(...)` for XDG-themed tray icons and routes raw IconPixmap blobs through `IconImageProvider::setImage` for QML binding.

The QML singleton (`IconThemeResolver` under `Phosphor.Service.IconTheme 1.0`) is registered for future bar widgets and third-party shells. The bundled `examples/phosphor-shell/` does not import it yet.
