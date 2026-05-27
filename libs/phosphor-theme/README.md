<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-theme

> Active design-token store for Phosphor shells. M3, ANSI 16, and brand
> gradient extensions. Ships a hot-reloadable `PaletteStore`, a matugen
> subprocess wrapper, a `{{token[.field]}}` template renderer, and the
> `Phosphor.Theme` QML module.

## Responsibility

The shell binds to colors through tokens, not literals. `phosphor-theme`
owns the live token map. It keeps that map in sync with matugen output,
user-edited JSON, and in-process producers.

- **One token map per engine.** `PaletteStore` is registered as a
  `QML_SINGLETON` and ships the canonical Phosphor dark palette as its
  built-in default. QML reads via `Phosphor.Theme`'s `Theme` singleton.
  C++ reads through `IThemeService::palette()`.
- **Three input paths land at the same map.** JSON load via
  `loadFromFile` and `QFileSystemWatcher`. In-process push via
  `applyTokens(QVariantMap)`. The matugen subprocess via
  `MatugenRunner::paletteReady`, which plumbs into `applyTokens`. Each
  path is testable in isolation.
- **Atomic-rename safe.** Editors that save via a temp file plus rename
  are handled. The watcher re-arms on every `fileChanged` so vim and
  emacs both fire exactly one reload per save.
- **Token name contract.** `TokenNames::*` are the canonical strings.
  Consumers reference these constants rather than literal map keys. A
  renamed token surfaces as a compile error at every call site.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorTheme::IThemeService`    | Abstract service. Methods are `palette()`, `token()`, `loadFromJson()`, `loadFromFile()`, `applyTokens()`, `resetToDefaults()` |
| `PhosphorTheme::TokenNames`       | `constexpr` token name strings. Covers the M3 surface ramp, accents, status ramp, and brand-gradient stops |
| `PhosphorTheme::PaletteStore`     | Concrete `IThemeService` with built-in dark defaults, JSON parsing, file watching, and QML singleton registration |
| `PhosphorTheme::MatugenRunner`    | `QProcess` wrapper around `matugen image <wp> --json hex`. Emits `paletteReady(tokens, wallpaper)` and `failed(wp, reason)` |
| `PhosphorTheme::TemplateEngine`   | Static renderer for `{{token[.field]}}` substitution. Supports the `hex`, `hexa`, `r`, `g`, `b`, `alpha`, `rgb`, and `rgba` field variants |
| `Phosphor.Theme.Theme`            | QML singleton. Color tokens by name such as `Theme.primary`, `Theme.on_surface`, `Theme.brand_stop_0`. Bindings re-evaluate on `paletteChanged` |
| `Phosphor.Theme.Tokens`           | Non-color tokens. Spacing, radius, elevation, typography |
| `Phosphor.Theme.Motion`           | M3 duration tokens plus bezier easing curves named `standard`, `emphasized`, `decelerated`, `accelerated` |
| `Phosphor.Theme.StateLayer`       | Interactive-state opacities. Properties are `hover`, `focus`, `pressed`, `dragged`, `disabled_content`, `disabled_container` |

## Typical use

**C++: load a palette and react to changes.**

```cpp
#include <PhosphorTheme/PaletteStore.h>

using namespace PhosphorTheme;

PaletteStore store;                              // built-in dark defaults active
const QString palettePath = "/path/to/palette.json";
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
`palette` map so bindings update live when the store reloads. The Design
notes section below explains the binding-tracking model.

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
  `Theme.*` accessors index into the `palette` Q_PROPERTY, which has
  `NOTIFY paletteChanged`. They never call `PaletteStore.token("...")`.
  That overload is a `Q_INVOKABLE` and is invisible to QML's dependency
  tracker. Bindings on it would not re-evaluate when the palette
  reloads. The same rule applies in any QML downstream of this module.
  Prefer `Theme.<name>` over `paletteStore.token(name)`.
- **Per-engine singleton, not a process global.** `PaletteStore` is a
  `QML_SINGLETON`. The engine owns one instance per `QQmlEngine`. Tests
  and shells that need an alternate service register an `IThemeService`
  instance with `qmlRegisterSingletonInstance` before the
  `Phosphor.Theme` module is imported.
- **Merge semantics, not replace.** `loadFromJson` and `applyTokens`
  insert the supplied tokens over the active map. Tokens absent from
  the new payload keep their previous value. Matugen omits brand-
  gradient and ANSI status colors. Those Phosphor extensions stay at
  their previous value across wallpaper changes.
- **Two JSON shapes accepted.** The wrapped form is
  `{ "tokens": { ... } }`. The flat form is
  `{ "primary": "...", ... }` at the top level. The wrapper leaves
  room for sibling metadata. The wrapper takes precedence when both
  could match.
- **Matugen schema variants.** The parser handles wrapped
  `colors.{dark,light}`, single-mode `colors.{token}`, and bare
  mode-at-root layouts so the runner stays compatible across the
  matugen versions seen in the wild.
- **Unknown tokens in templates surface, not silently disappear.**
  `TemplateEngine` keeps the `{{...}}` placeholder in the output and
  logs to `qWarning`. Unknown `.field` values fall back to hex with a
  warning rather than aborting.

## Dependencies

- `QtCore`, `QtGui`, `QtQml`. Zero Phosphor deps. This is a leaf
  library.
- Optional runtime is the external `matugen` binary on `$PATH`. Only
  needed when `MatugenRunner` is used. The library never invokes matugen
  unsolicited.

## See also

- `examples/phosphor-theme-demo/`, GUI swatch sheet that hot-reloads
  from disk and from in-memory pushes.
- `examples/phosphor-theme-cli/`, headless driver. Provides
  `set-wallpaper`, `dump`, `render-template`, and `cycle` subcommands.
