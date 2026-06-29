<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# PhosphorShellWidgets

The `Phosphor.Widgets` atom library: the Material 3 building blocks every
Phosphor shell surface composes. Pure QML, themed entirely through
[`phosphor-theme`](../phosphor-theme/README.md)'s `Theme` / `Motion` /
`StateLayer` singletons, so a palette or motion retune propagates to every
widget with no per-widget edit.

Phase 3.1 deliverable per
[`docs/phosphor-shell-design/04-implementation-plan.md`](../../docs/phosphor-shell-design/04-implementation-plan.md).

## Responsibility

Provide a small, consistent set of interactive atoms (buttons, slider,
text field, card, pill) plus the shared visual primitives they are built
from (ripple + state layer, elevation shadow). Higher-level surfaces (the
bar, launcher, control center, OSDs) assemble these rather than
re-rolling button and slider styling per surface.

The library owns presentation only. It holds no business logic and no
service bindings; a host wires an atom's `clicked` / `moved` / `toggled`
signal to whatever controller drives it.

## Key types

| Component            | Role                                                                                  |
|----------------------|---------------------------------------------------------------------------------------|
| `PhosphorButton`     | M3 button in four variants (`Filled`, `Tonal`, `Outlined`, `Text`).                   |
| `PhosphorSlider`     | Continuous horizontal slider; drag the handle or tap the track. Emits `moved`.        |
| `PhosphorTextField`  | Outlined single-line input; focus thickens and tints the outline; placeholder.        |
| `PhosphorCard`       | Elevated rounded surface container; children land in a padded content area.           |
| `PhosphorPill`       | Compact fully-rounded chip / toggle; outlined when unselected, filled when selected.  |
| `PhosphorRipple`     | Shared interaction layer: hover / press state-layer tint + expanding press ripple.    |
| `ElevationShadow`    | M3 elevation shadow (levels 0..5), used as a `layer.effect` on any rounded surface.   |
| `BarCanvas`          | Connected-corner bar surface; its bottom edge weaves into `sockets` so popouts grow out of it as one Shape. |
| `ConnectedShape`     | Generic `Shape` painter for an SVG path string; the renderer behind `BarCanvas`.       |
| `ConnectedCorner`    | Standalone concave / convex quarter-fillet primitive for hand-composing joins.        |
| `ConnectorGeometry.js` | Pure path math: builds the connected-corner SVG `d` string from socket descriptors. |

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

Set `enabled: false` on any atom for its disabled state; container and
content drop to the M3 disabled opacities (`StateLayer.disabled_container`
/ `StateLayer.disabled_content`).

## Design notes

- **Tokens, not literals.** Every colour reads `Theme.*`, every animation
  reads `Motion.*`, every state opacity reads `StateLayer.*`. There are no
  hard-coded colours, durations, or easing curves. Live retinting works
  because the atoms index the `Theme` singleton's `palette` map (a
  `NOTIFY`-backed property), the same binding-tracking rule documented in
  `phosphor-theme`.
- **Custom controls, not restyled `QtQuick.Controls`.** The atoms are
  built from primitives (`Rectangle`, `Text`, `TextInput`, handlers)
  rather than restyling Controls, so theming is total and there is no
  style-engine fallback path to fight.
- **`PhosphorRipple` is the shared interaction layer.** Buttons and pills
  embed it for the hover / press tint and the touch ripple, and re-emit
  its `tapped` as their own `clicked`. Known limitation: `Item.clip` is
  rectangular, so the expanding ripple circle is not masked to a rounded
  corner radius. The resting state-layer tint honours `radius`; only the
  transient ripple sweep is unclipped. A rounded clip (e.g. a `MultiEffect`
  mask sourced from a `ConnectedShape`) is the eventual fix; it is not yet
  wired in, as the spill is subtle and per-control masking has a real cost.
- **Elevation is two halves.** `ElevationShadow` is the shadow half: a
  `MultiEffect` tuned for the six M3 elevation tiers, used as a
  `layer.effect` so the engine wires the layered item into its `source`
  and it tracks geometry and corner radius automatically. The colour half
  is the M3 surface tint overlay: `PhosphorCard` tints its
  `surface_container` fill with `Theme.surface_tint` at the per-tier
  elevation-overlay opacity, so a higher card reads as both more shadowed
  and more tinted. `surface_tint` defaults to the primary accent (the M3
  default) but honours an explicit `surface_tint` palette token when one
  is present (e.g. from matugen), so the tint is themeable without binding
  the literal primary.
  `PhosphorCard` only enables the layer (shadow) pass when
  `elevation > 0`.
- **Host owns state.** `PhosphorPill` does not flip its own `selected`,
  and `PhosphorSlider`/`PhosphorTextField` expose the value/text for the
  host to bind. The atoms emit intent; the host owns the source of truth.
- **Connected-corner geometry is a path string, not declarative arcs.**
  `ConnectorGeometry.js` builds the outline as an SVG `d` string and
  `ConnectedShape` renders it via `PathSvg`, because a `Shape` cannot
  generate N pocket arcs declaratively for a variable socket count. This
  matches the design mockups (also SVG paths) command-for-command. The
  morph is driven by the host animating a socket's `depth`; the path
  recomputes reactively and `Shape.CurveRenderer` antialiases the concave
  joins. The pocket degrades to a flat edge at `depth <= 0.5`, so an
  open-from-zero animation grows the pocket smoothly. The popout body is
  drawn over the pocket by the host (e.g. the bar-canvas demo), so the
  painted socket and the content read as one surface.

## Dependencies

- Qt6 ≥ 6.6 Core / Gui / Qml / Quick. `ElevationShadow` uses
  `QtQuick.Effects` (`MultiEffect`), which ships with Qt6::Quick;
  `ConnectedShape` / `BarCanvas` use `QtQuick.Shapes` (`Qt6::QuickShapes`).
- `phosphor-theme` (`Phosphor.Theme` QML module) for the `Theme`,
  `Motion`, and `StateLayer` singletons. In-tree builds link the theme
  QML plugin automatically; the module is static and in-tree-only today.

## Status

Phase 3.1 (atoms) and Phase 3.2 (connected-corner geometry) are in the
tree, each with an acceptance demo
(`examples/phosphor-widgets-kitchen-sink/`,
`examples/phosphor-bar-canvas-demo/`). The Phase 3 gate (all four 3.x
examples runnable, `phosphor-ui-primitives-0.1` tag) still requires 3.3
(OSD framework) and 3.4 (toast framework).
