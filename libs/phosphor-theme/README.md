<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: LGPL-2.1-or-later
-->

# PhosphorTheme

Token store + `Phosphor.Theme` QML module for Phosphor shells. Phase 1.1 of
the shell roadmap. See `docs/phosphor-shell-design/04-implementation-plan.md`.

## What's here

- `PaletteStore` (C++ `QML_SINGLETON`) holds the active token map,
  hot-reloads from a watched JSON file, applies parsed token maps from
  any in-process source via `applyTokens`.
- `IThemeService` is the interface seam for test mocks and alternate
  implementations (remote-driven, kcolorscheme-bridged, etc.).
- `MatugenRunner` wraps `matugen image <wp> --json hex`, parses three
  schema flavours, emits `paletteReady(tokens, wallpaper)`.
- `TemplateEngine` renders `{{token[.field]}}` against the active
  palette for the matugen fan-out pipeline.
- `Phosphor.Theme` QML module ships the `Theme`, `Tokens`, `Motion`,
  `StateLayer` singletons.

Hardcoded named palettes (e.g. dark / light / sunset / forest) are
demo-only. They live in `examples/phosphor-theme-demo/` and are not part
of this library's public API.

## Proving hot-reload works

Three test paths cover the lib end-to-end. Use whichever fits the
situation.

### 1. In-demo preset buttons (no external setup)

```sh
build/bin/phosphor-theme-demo
```

Click any of the **dark / light / sunset / forest** buttons at the top.
Every swatch, the gradient strip, the window background, and the button
chrome retint in one frame. This exercises
`PaletteStore.applyTokens(QVariantMap)`, the same code path
`MatugenRunner` uses on a wallpaper change.

The preset definitions live in
`examples/phosphor-theme-demo/PresetPalettes.h` and are registered into
the `Phosphor.ThemeDemo` QML module, not `Phosphor.Theme`.

### 2. CLI cycle on a palette directory (drive a running demo headlessly)

Build a directory of palette JSON files (any combination of hand-edited
files, matugen outputs, or `phosphor-theme-cli dump` snapshots). Then:

```sh
# Terminal 1
build/bin/phosphor-theme-demo

# Terminal 2 — walk every *.json in the dir once, ~1.5s apart
build/bin/phosphor-theme-cli cycle ~/.local/share/phosphor/palettes --once --apply

# Or loop forever with a custom interval until Ctrl-C
build/bin/phosphor-theme-cli cycle ./my-palettes --apply --interval 800
```

`--apply` writes each loaded palette to
`~/.local/share/phosphor/palettes/current.json`. The demo's
`QFileSystemWatcher` fires within ~100 ms and `PaletteStore` reloads.

Without `--apply` or `--out`, cycle just logs each file (useful for
sanity-checking a directory before driving the demo).

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

The lowest-tech proof, no CLI needed:

```sh
build/bin/phosphor-theme-cli dump > ~/.local/share/phosphor/palettes/current.json
build/bin/phosphor-theme-demo &

# Edit the file in your editor of choice. Change any color value, save.
# The matching swatch retints within ~100 ms.
```

`PaletteStore` handles atomic-rename saves (vim, emacs) by re-adding the
file to the watcher on each `fileChanged` notification. See
`src/palettestore.cpp`.

## Unit tests

```sh
ctest --test-dir build -R "test_palettestore|test_templateengine|test_matugenrunner" --output-on-failure
```

23 cases across the three test binaries.
