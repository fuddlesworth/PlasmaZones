<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# PhosphorShellWidgets

The `Phosphor.Widgets` atom library: the spectrum primitives every shell
surface is drawn with, plus a few interactive atoms. Pure QML, themed
entirely through [`phosphor-theme`](../phosphor-theme/README.md)'s `Theme`
/ `Tokens` / `Motion` / `Spectrum` singletons, so a palette or motion retune
propagates to every widget with no per-widget edit. The design they
implement is `docs/phosphor-shell-design/05-visual-identity.md`.

## Responsibility

Provide the primitives the identity is made of (the rail, the stroke, the
underline, the tabular value, the placement miniature, the settle spring,
the decoration slot) and a small set of interactive atoms (button, slider,
text field, card). Higher-level surfaces (the bar, launcher, control center,
OSDs, toasts) assemble these rather than re-rolling styling per surface.

The library owns presentation only. It holds no business logic and no
service bindings. A host wires an atom's `clicked` / `moved` signal to
whatever controller drives it.

## Key types

| Component | Role |
|---|---|
| `SpectrumRail` | The 2 px gradient rail on a screen edge, sampling the spectrum by position; `gleam` runs the idle sweep, `sliceStart`/`sliceEnd` show a scrolling strip's lens. |
| `SpectrumStroke` | The 1 px inset stroke that gives chrome its edge: rest opacity, full when `active`, white when `focused`; hue from `t`. |
| `SpectrumUnderline` | A 2 px underline whose length is a value and whose hue is the state axis; `tick()` flashes it. |
| `TabularText` | A value in the mono face with tabular figures; `tickOnChange` underlines a changed digit. |
| `PlacementMiniature` | A screen's placement map (zones, tiles or strip columns) as a miniature; `MiniatureEdges.js` morphs matched edges between modes. |
| `SettleAnimation` | The positional spring (`shell.settle`) for a `Behavior`; retargets from the current velocity. |
| `DecorationSlot` | Where a surface pack lands on chrome: names the frame item and the surface path, instantiates the host's decoration Component. |
| `PhosphorButton` | Button in four variants (`Filled`, `Tonal`, `Outlined`, `Text`). |
| `PhosphorSlider` | Continuous horizontal slider; the knob settles on the spring. Emits `moved`. |
| `PhosphorTextField` | Outlined single-line input with a placeholder; focus lights the stroke. |
| `PhosphorCard` | Rounded surface container with a stroke for depth; children land in a padded content area. |
| `PhosphorRipple` | Shared interaction layer: hover / press tint and the press ripple. |

## Typical use

```qml
import Phosphor.Widgets

PhosphorCard {
    elevation: 2

    ColumnLayout {
        PhosphorTextField { placeholderText: i18n("Name") }
        PhosphorSlider { from: 0; to: 100; value: 40; onMoved: (v) => model.level = v }
        PhosphorButton {
            text: i18n("Apply")
            variant: PhosphorButton.Filled
            onClicked: controller.apply()
        }
    }
}
```

Set `enabled: false` on any atom for its disabled state. The container and
content then drop to the disabled opacities (`StateLayer.disabled_container`
/ `StateLayer.disabled_content`).

## Design notes

- **Tokens, not literals.** Every colour reads `Theme.*` or `Spectrum.*`,
  every animation reads `Motion.*`, every state opacity reads `StateLayer.*`.
  Live retinting works because the atoms index the `Theme` singleton's
  `palette` map (a `NOTIFY`-backed property).
- **Colour is a coordinate.** A stroke's hue comes from `t`: the rail axis
  (x over the screen width) for anything on the bar, the state axis (a
  value 0..1) for anything that shows a level, the structure axis for a
  miniature's cells. Focus and urgency are white and rose, the only colours
  that are not sampled.
- **Depth is a stroke, not a shadow.** Chrome carries no drop shadow and no
  radius above 10 px (`Tokens.radius_edge/tile/container/mini`).
  `PhosphorCard.elevation` picks the surface tint tier and lights the stroke;
  the M3 shadow tiers in `Tokens` are for settings pages.
- **Enter fast, release slow.** A stroke goes to full in 90 ms with a hair
  of overshoot and releases over 360 ms; positions and sizes settle on the
  spring. Reduced motion halves the tails and drops the overshoot and the
  gleam.
- **Custom controls, not restyled `QtQuick.Controls`.** The atoms are built
  from primitives (`Rectangle`, `Text`, `TextInput`, handlers), so theming
  is total.
- **Host owns state.** `PhosphorSlider` and `PhosphorTextField` expose the
  value / text for the host to bind; the atoms emit intent. `PhosphorSlider`
  follows the `QtQuick.Controls.Slider` convention: set `value` as the
  initial position and respond to `moved`.
- **Keyboard and focus.** Every interactive atom is Tab-focusable when
  enabled. `PhosphorButton` activates on Space / Enter / Return;
  `PhosphorSlider` moves on the arrows by `stepSize` and jumps to the ends
  on Home / End; `PhosphorTextField` delegates focus to its inner input.
- **Decoration is the host's.** `DecorationSlot` is a seam: the library
  names the frame item (`shaderAnchor: true`) and the surface path, and the
  composition root supplies the Component that draws the pack chain, so
  the library never depends on the shell process's rendering stack.

## Dependencies

- Qt6 ≥ 6.6 Core / Gui / Qml / Quick. `PhosphorRipple` uses
  `QtQuick.Shapes` (`Qt6::QuickShapes`). `SettleAnimation` uses
  `org.phosphor.animation` (`PhosphorMotionAnimation`).
- `phosphor-theme` (`Phosphor.Theme` QML module) for the singletons.
  In-tree builds link the theme QML plugin automatically. The module is
  static and in-tree-only today.
- `org.kde.kirigami` at runtime, for `PlacementMiniature`'s app glyphs
  (`Kirigami.Icon`). Declared as a `DEPENDENCIES` entry, never `IMPORTS`:
  Kirigami's `Theme` attached type would shadow ours.

## Status

The spectrum primitives and the atoms ship with the identity (phases 1 to 5
of `docs/phosphor-shell-design/04-implementation-plan.md`). The first
design's connected-corner geometry, `ElevationShadow` and `PhosphorPill`
were retired in phase 6.
