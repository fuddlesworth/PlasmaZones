<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-theme

> Active design-token store for Phosphor shells: M3 + ANSI 16 + brand
> gradient extensions, a hot-reloadable `PaletteStore`, a matugen
> subprocess wrapper, a `{{token[.field]}}` template renderer, and the
> `Phosphor.Theme` QML module.

## Responsibility

The shell binds to colors through tokens, not literals. `phosphor-theme`
owns the live token map and the machinery that keeps it in sync with
external sources (matugen output, user-edited JSON, in-process producers).

- **One token map per engine.** `PaletteStore` is registered as a
  `QML_SINGLETON` and ships the canonical Phosphor dark palette as its
  built-in default. QML reads via `Phosphor.Theme`'s `Theme` singleton;
  C++ reads through `IThemeService::palette()`.
- **Three input paths land at the same map.** JSON load (`loadFromFile`
  + `QFileSystemWatcher`), in-process push (`applyTokens(QVariantMap)`),
  matugen subprocess (`MatugenRunner::paletteReady` plumbs into
  `applyTokens`). Each path is testable in isolation.
- **Atomic-rename safe.** Editors that save via temp-file + rename
  (vim, emacs) re-arm the watcher on every `fileChanged` so a single
  save fires exactly one reload.
- **Token name contract.** `TokenNames::*` are the canonical strings;
  consumers reference these constants rather than literal map keys, so
  a renamed token surfaces as a compile error at every call site.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorTheme::IThemeService`    | Abstract service: `palette()`, `token()`, `loadFromJson()`, `loadFromFile()`, `applyTokens()`, `resetToDefaults()` |
| `PhosphorTheme::TokenNames`       | `constexpr` token name strings (M3 surface ramp, accents, status, brand-gradient stops) |
| `PhosphorTheme::PaletteStore`     | Concrete `IThemeService` with built-in dark defaults, JSON parsing, file watching, QML singleton registration |
| `PhosphorTheme::MatugenRunner`    | `QProcess` wrapper around `matugen image <wp> --json hex`; emits `paletteReady(tokens, wallpaper)` and `failed(wp, reason)` |
| `PhosphorTheme::TemplateEngine`   | Static renderer for `{{token[.field]}}` substitution; supports `hex` / `hexa` / `r` / `g` / `b` / `alpha` / `rgb` / `rgba` field variants |
| `Phosphor.Theme.Theme`            | QML singleton, color tokens by name (`Theme.primary`, `Theme.on_surface`, `Theme.brand_stop_0`...). Bindings re-evaluate on `paletteChanged` |
| `Phosphor.Theme.Tokens`           | Non-color tokens: spacing, radius, elevation, typography |
| `Phosphor.Theme.Motion`           | M3 duration tokens + bezier easing curves (`standard`, `emphasized`, `decelerated`, `accelerated`) |
| `Phosphor.Theme.StateLayer`       | Interactive-state opacities (`hover`, `focus`, `pressed`, `dragged`, `disabled_content`) |

## Typical use

**C++: load a palette and react to changes.**

```cpp
#include <PhosphorTheme/PaletteStore.h>

using namespace PhosphorTheme;

PaletteStore store;                              // built-in dark defaults active
store.loadFromFile(palettePath);                 // arms QFileSystemWatcher

QObject::connect(&store, &PaletteStore::paletteChanged, [&]() {
    qDebug() << "palette reloaded:" << store.palette().size() << "tokens";
});

QObject::connect(&store, &PaletteStore::loadError,
                 [](const QString &path, const QString &reason) {
                     qWarning() << "load failed:" << path << reason;
                 });
```

**QML: bind to tokens.** The named accessors route through the tracked
`palette` map so bindings update live when the store reloads (see Design
notes for why).

```qml
import QtQuick
import Phosphor.Theme

Rectangle {
    color: Theme.surface_container
    border.color: Theme.outline_variant
    radius: Tokens.radius_m

    Text {
        text: "Themed"
        color: Theme.on_surface
        font.pixelSize: Tokens.font_size_label_l
        font.family: Tokens.font_family
    }

    Behavior on color {
        ColorAnimation {
            duration: Motion.duration_medium_2
            easing: Motion.standard
        }
    }
}
```

**Wallpaper-driven retint via matugen.**

```cpp
#include <PhosphorTheme/MatugenRunner.h>
#include <PhosphorTheme/PaletteStore.h>

MatugenRunner runner;
PaletteStore store;

QObject::connect(&runner, &MatugenRunner::paletteReady,
                 &store, [&](const QVariantMap &tokens, const QString &) {
                     store.applyTokens(tokens);   // same merge semantics as loadFromJson
                 });

runner.run(QStringLiteral("/path/to/wallpaper.jpg"));
```

**Template fan-out for external apps.**

```cpp
#include <PhosphorTheme/TemplateEngine.h>

// gtk-3.0.css.template contains: @define-color accent {{primary.rgb}};
PhosphorTheme::TemplateEngine::renderFile(
    QStringLiteral("/path/to/gtk-3.0.css.template"),
    QStringLiteral("/path/to/gtk-3.0/gtk.css"),
    store.palette());
```

## Design notes

- **QML bindings only track property reads, not method calls.** The
  `Theme.*` accessors index into `palette[...]` (a `Q_PROPERTY` with
  `NOTIFY paletteChanged`), never `PaletteStore.token("...")`. The latter
  is a `Q_INVOKABLE` and is invisible to QML's dependency tracker, so
  bindings on it would not re-evaluate when the palette reloads. The
  same rule applies in any QML downstream of this module: prefer
  `Theme.<name>` over calling `paletteStore.token(name)`.
- **Per-engine singleton, not a process global.** `PaletteStore` is a
  `QML_SINGLETON` (one instance per `QQmlEngine`). Tests and shells that
  need an alternate service register an `IThemeService` instance with
  `qmlRegisterSingletonInstance` before the `Phosphor.Theme` module is
  imported.
- **Merge semantics, not replace.** `loadFromJson` / `applyTokens` insert
  the supplied tokens over the active map; tokens absent from the new
  payload keep their previous value. Matugen omits brand-gradient and
  ANSI status colors; those Phosphor extensions stay at their previous
  value across wallpaper changes.
- **Two JSON shapes accepted.** `{ "tokens": { ... } }` (matugen-style,
  leaves room for sibling metadata) or a flat `{ "primary": "...", ... }`
  top-level map. The wrapper takes precedence when both could match.
- **Matugen schema variants.** The parser handles wrapped
  `colors.{dark,light}`, single-mode `colors.{token}`, and bare
  mode-at-root layouts so the runner stays compatible across the
  matugen versions seen in the wild.
- **Unknown tokens in templates surface, not silently disappear.**
  `TemplateEngine` keeps the `{{...}}` placeholder in the output and
  logs to `qWarning`. Unknown `.field` values fall back to hex with a
  warning rather than aborting.

## Dependencies

- `QtCore`, `QtGui`, `QtQml`. Zero Phosphor deps; this is a leaf
  library.
- Optional runtime: the external `matugen` binary on `$PATH`, only when
  `MatugenRunner` is used. The library never invokes matugen
  unsolicited.

## See also

- `examples/phosphor-theme-demo/`, GUI swatch sheet that hot-reloads
  from disk and from in-memory pushes.
- `examples/phosphor-theme-cli/`, headless driver
  (`set-wallpaper` / `dump` / `render-template` / `cycle`).
