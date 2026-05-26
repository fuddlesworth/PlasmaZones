<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: LGPL-2.1-or-later
-->

# PhosphorTheme

Token store + `Phosphor.Theme` QML module for Phosphor shells. Phase 1.1 of
the shell roadmap — see
`docs/phosphor-shell-design/04-implementation-plan.md`.

## What's here

- `PaletteStore` (C++ `QML_SINGLETON`) — holds the active token map,
  hot-reloads from a watched JSON file, applies parsed token maps from any
  in-process source via `applyTokens`.
- `IThemeService` — interface seam for test mocks / alternate
  implementations (remote-driven, kcolorscheme-bridged, etc.).
- `MatugenRunner` — wraps `matugen image <wp> --json hex`, parses three
  schema flavours, emits `paletteReady(tokens, wallpaper)`.
- `TemplateEngine` — `{{token[.field]}}` renderer for matugen fan-out.
- `PresetPalettes` — four contrasting hand-curated palettes (dark / light
  / sunset / forest), exposed as `Q_INVOKABLE` accessors.
- `Phosphor.Theme` QML module: `Theme`, `Tokens`, `Motion`, `StateLayer`
  singletons.

## Proving hot-reload works

Three test paths cover the lib end-to-end. Use whichever fits the
situation.

### 1. In-demo preset buttons (no external setup)

```sh
build/bin/phosphor-theme-demo
```

Click any of the **Dark / Light / Sunset / Forest** buttons at the top.
Every swatch, the gradient strip, the window background, and the button
chrome retint in one frame. This exercises
`PaletteStore.applyTokens(QVariantMap)` — the same code path
`MatugenRunner` uses on a wallpaper change.

### 2. CLI cycle mode (drive a running demo headlessly)

In one terminal:

```sh
build/bin/phosphor-theme-demo
```

In another:

```sh
# Cycle through all four presets once, ~1.5s apart, written to the path
# the demo watches.
build/bin/phosphor-theme-cli cycle --once --apply

# Or loop forever with a custom interval until Ctrl-C.
build/bin/phosphor-theme-cli cycle --apply --interval 800
```

`--apply` writes each preset to
`~/.local/share/phosphor/palettes/current.json`. The demo's
`QFileSystemWatcher` fires within ~100 ms and `PaletteStore` reloads.

### 3. Matugen round-trip (full pipeline, requires `matugen` on PATH)

```sh
build/bin/phosphor-theme-demo &

build/bin/phosphor-theme-cli set-wallpaper ~/Pictures/some-wallpaper.jpg --apply
# demo retints to the wallpaper-derived palette
build/bin/phosphor-theme-cli set-wallpaper ~/Pictures/different.jpg --apply
# demo retints again
```

Matugen failure modes (binary missing, image missing, unexpected JSON
shape) surface as structured stderr from the CLI. The demo's bottom
status bar shows any JSON parse errors when the watched file goes
malformed mid-edit.

## Manual edit

The lowest-tech proof — works without the CLI at all:

```sh
build/bin/phosphor-theme-cli dump > ~/.local/share/phosphor/palettes/current.json
build/bin/phosphor-theme-demo &

# Edit the file in your editor of choice; change any color value, save.
# The matching swatch retints within ~100 ms.
```

`PaletteStore` handles atomic-rename saves (vim, emacs) by re-adding the
file to the watcher on each `fileChanged` notification — see
`src/palettestore.cpp`.

## Unit tests

```sh
ctest --test-dir build -R "test_palettestore|test_templateengine|test_matugenrunner" --output-on-failure
```

23 cases across the three test binaries. Run from any working directory.
