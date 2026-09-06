<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 04: Implementation Record

How the spectrum identity (`05-visual-identity.md`, `identity/A1` to `A4`)
was applied to the shell, phase by phase, with what each phase proved live
and what it could not. The library and service groundwork that preceded it
(the theme, popout, registry and IPC libraries, the fourteen service
libraries, the atoms) is history now and lives in git; this document starts
where the identity does.

## How the phases were run

- One phase per commit on the `shell-design` worktree, each green on the full
  `ctest` run under `dbus-run-session` and checked live in the nested
  harness (`scripts/nested-shell/run-shell.sh` on a private socket) before
  it was committed.
- Daemon surfaces first, shell consumers second, with a capability latch in
  the shell for every new daemon method so an older daemon degrades rather
  than breaks.
- Nothing is a guest on another desktop. Where a feature seemed blocked
  because "Plasma owns that", the answer was to own it in the shell (the
  wallpaper surface, the lock, the agent).

## Phases

### Phase 1: theme and atoms (`e15a3f0d5`)

Spectrum tokens (`Spectrum.qml`, the four radii, the strokes, the bar
geometry, the motion primitives), the widgets (`SpectrumRail`,
`SpectrumStroke`, `SpectrumUnderline`, `TabularText`, `PlacementMiniature`),
the `PlacementMap` D-Bus model, the rail bar with its pane API, the control
center rails, the launcher viewfinder, the OSD edge bands, the toasts under a
band, the power word column.

### Phase 2: engine-placed pane and window-edge bands (`4de6490b9`)

Daemon: `Scrolling.stripModelJson`, `focusColumnAt`, `scrollViewByPx`;
`Tiling.currentTilesJson`, `managedFocusedWindow`. Shell: the control center
as a frameless toplevel the engine places through a seeded rule
(`PanePopoutTransport`, `PaneRules`), the tether, compositor blur behind the
band, OSD and toast hosts mounted per screen with input regions.

### Phase 3: map interactions (`7927a6d88`)

Daemon: `WindowTracking.activateWindow`, `getWindowMetadata`,
`moveWindowToDesktop`, `getUrgentWindows` and their signals;
`Scrolling.moveColumnTo`; `WindowDrag.registerDropProxy`. Shell: activate,
drag-swap, float and the context menu on the miniature, the matched-edge
morph between modes, urgency on the rail, the expanded map pane.

### Phase 4: the remaining surfaces (`4c807276f`)

Lock (session-lock surface in the QPA, region layout, unlock fill before
release), dashboard (every desktop's map, one scale transition), cheatsheet
(chords on the live map from `Control.getShortcutsJson`), wallpaper picker
with a shell-owned background-layer wallpaper surface, polkit prompt on the
window edge with exclusive keyboard focus.

### Phase 5: motion, gestures, packs (`20468abd4`, `e7d3e60e0`, `46f4f5c35`, `bf25966ef`)

- 5a: `ShellMotion` (the settle spring as `shell.settle`, reduced motion from
  the settings portal), `SettleAnimation`, `FontFaces`, the OSD readout
  outline.
- 5b: touchpad gestures registered by the effect, relayed over
  `CompositorBridge.reportGesture` and `ShellGestures`; toast swipe; rail
  two-finger step.
- 5c: surface packs on the chrome through `ShellChrome` and `DecorationSlot`,
  the `shell.phosphor.*` tree paths with seeded defaults and settings cards.

### Phase 6: retiring the first design

The connected-corner geometry (`BarCanvas`, `ConnectedShape`,
`ConnectedCorner`, `ConnectorGeometry.js`), `ElevationShadow`, `PhosphorPill`
and the bar-canvas demo are gone, with the v1 mockups. These documents were
rewritten for the shell that exists.

## What each phase proved live

| Verified in the nested harness | Not verifiable there |
|---|---|
| Bar rail and band, placement map in all three modes, mode morph | Lock (`ext_session_lock_manager_v1` is not advertised by the virtual backend) |
| Engine-placed pane and tether, blur behind the band | Polkit prompt (the host session's agent owns the seat) |
| OSD bands on the focused window, toasts, launcher, dashboard, cheatsheet, picker with live wallpaper preview | Real touchpad gestures (the relay is driven by calling `reportGesture`) |
| Packs on the bar band, the OSD band, the toast card, the launcher, the picker strip | |
| The gestures capability from the rebuilt effect | |

## What is next

See `02-gap-analysis.md`. The first three items there (compositor-drawn
chrome packs, the two missing decoration slots, gesture progress) are the
ones that extend the identity; the surface gaps are ordinary feature work.

## Keeping this record honest

- A phase is not done until its unit tests, the full suite and a live capture
  agree. A green suite alone has been wrong twice in this work (a stale test
  binary, a stale effect binary).
- When a scripted edit lands, check `git status` before building. One batch
  of effect edits was accepted by the tooling and never reached the tree.
- The mockups in `mockups-v2/` are the reference. If the lived design
  deviates, update them or note the deviation in their README.
