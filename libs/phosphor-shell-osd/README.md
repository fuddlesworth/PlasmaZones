<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# PhosphorShellOsd

The `Phosphor.OSD` on-screen-display framework: the transient overlays a
shell flashes for volume, brightness, mic-mute, caps-lock, and the like.
Pure QML, themed through [`phosphor-theme`](../phosphor-theme/README.md)
and built on the [`phosphor-shell-widgets`](../phosphor-shell-widgets/README.md)
atoms.

Phase 3.3 deliverable per
[`docs/phosphor-shell-design/04-implementation-plan.md`](../../docs/phosphor-shell-design/04-implementation-plan.md).

## Responsibility

Own the lifecycle of the transient OSD surface: show one OSD at a time,
hold it for a timeout, fade it out, and route triggers to the right
screen. Debounce/dedupe repeated triggers so spinning a volume key keeps
one OSD alive and refreshed instead of stacking surfaces.

The framework owns presentation and timing only. The OSD *content* is
supplied by delegates (the four built-ins here, or plugin-contributed
ones), and the *triggers* come from the host (a hotkey handler, an IPC
call, a service signal).

## Key types

| Component       | Role                                                                                       |
|-----------------|--------------------------------------------------------------------------------------------|
| `OSDHost`       | Per-screen OSD surface manager: one-at-a-time display, hold timer, debounce/dedupe, fade, screen routing. |
| `OSDCard`       | Shared chrome: an elevated rounded card with a glyph, label, and optional progress bar.     |
| `VolumeOSD`     | Speaker glyph + progress; muted cross at zero. `value` is 0..100.                            |
| `BrightnessOSD` | Sun glyph + progress. `value` is 0..100.                                                     |
| `MicOSD`        | Microphone glyph; `active` true = muted (red slash + label).                                 |
| `CapsLockOSD`   | Caps-lock glyph; `active` true = on (primary-tinted + label).                                |

## Typical use

```qml
import Phosphor.OSD

OSDHost {
    id: osd
    anchors.fill: parent
    screenName: "DP-1"
    provider: registryProvider   // createOSD(kind, parent) -> Item
}

// elsewhere, on a volume key / IPC call:
osd.show("volume", 62, undefined, "DP-1")
```

`provider` is any object exposing `createOSD(kind, parent) -> Item`. In
the shell it is backed by a `Registry<IOSDFactory>` (the OSD demo wires
this and registers the four built-ins as `IOSDFactory` instances). A
delegate is expected to carry `value` (0..100) and/or `active` (bool).
`OSDHost` sets whichever it exposes.

The host owns the delegate's lifetime and destroys it on swap/hide, so a
provider MUST return a **destroyable** item. A C++ factory (the
`IOSDFactory` path) must set `QQmlEngine::setObjectOwnership(item,
QQmlEngine::JavaScriptOwnership)` before returning it. A QObject-parented
item defaults to `CppOwnership`, which makes the host's `destroy()` throw
("indestructible object") and leaves a ghost stacked behind the next OSD.

Multi-screen: instantiate one `OSDHost` per monitor (via
`Phosphor.Shell`'s `PerScreen`), give each its `screenName`, and pass the
same trigger to all. `show()`'s `targetScreen` argument routes it
(`""`/omitted = every screen).

## Design notes

- **Debounce/dedupe.** A repeated `show()` of the kind already up updates
  the delegate and restarts the hold timer rather than recreating it, so
  a held key streams smoothly through one surface. A different kind swaps
  the delegate.
- **Lifecycle via states.** `OSDHost` drives a `shown`/`hidden` state
  pair. The transitions animate opacity + scale with `phosphor-theme`
  Motion tokens, and the hide transition's tail destroys the delegate and
  emits `hidden(kind)`. The hold timer is what flips `shown -> hidden`. A
  kind swap also emits `hidden(previousKind)` synchronously from `show()`
  for the outgoing OSD, so the `shown`/`hidden` pairing stays symmetric.
- **Provider seam, not a hard registry dependency.** `OSDHost` takes a
  `provider` object, so the framework is testable with a fake and the
  registry wiring (IOSDFactory) lives in the host/demo, not the QML.
- **Routing is data, not topology.** Screens are addressed by name, so
  routing is unit-testable without real monitors.

## Dependencies

- Qt6 ≥ 6.6 Core / Gui / Qml / Quick; `QtQuick.Shapes` (`Qt6::QuickShapes`)
  for the OSD glyphs.
- `phosphor-theme` (`Phosphor.Theme`) for tokens and Motion;
  `phosphor-shell-widgets` (`Phosphor.Widgets`) for `ElevationShadow`.
  In-tree builds link their QML plugins automatically. This module is
  static and in-tree-only today.

## Status

Phase 3.3: in progress. `OSDHost`, the card chrome, and the four built-in
OSDs are in the tree with a QtQuickTest suite and an acceptance demo
(`examples/phosphor-osd-demo/`, driven by `phosphorctl call osd.show`).
The Phase 3 gate (all four 3.x examples runnable,
`phosphor-ui-primitives-0.1` tag) still requires 3.4 (toast framework).
