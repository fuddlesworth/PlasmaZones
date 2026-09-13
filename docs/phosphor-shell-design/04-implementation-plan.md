<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 04: Implementation Record

## Floating-shell redesign (mockups-v3)

The approved direction is implemented in six phases. Each implementation
phase was built and checked with its affected suites and the isolated nested
harness. The final phase runs the entire suite against the completed tree.

| Phase | Commit | Result |
|---|---|---|
| 1. Shared appearance and everyday panels | `11b8cfc04` | Floating materials, Phosphor/Paper/Ember, appearance persistence, connection rows, service-backed media visualization and calendar. |
| 2. Bounded Navigator | `2629bf5d8` | Complete window navigation for snapping, tiling and scrolling; readable fixed cards; constant-size bar mini with offscreen counts. |
| 3. Stage | `ce3e82053` | Full-screen workspace overview, inspector and real desktop transform with a map fallback. |
| 4. Shelf and surface consistency | `92c125542` | Three-column quick settings with narrow-screen reflow; launcher, notifications, OSD, power, picker, lock and polkit adopt the selected palette/material. |
| 5. Customization | `9693f02e8` | Saved widget regions/order/visibility, overflow controls, configurable fonts, preserved preferences across presets and live settings without source reloads. |
| 6. Regression and handoff | This final phase | Full build/test pass, final nested checks and updated design records. |

### Final result

`cmake --build build --parallel 8` succeeds. The completed tree passes
`dbus-run-session -- ctest --test-dir build --output-on-failure --parallel 4`:
538 tests registered, 537 passed and the existing
`test_surface_decoration_orientation` skipped. No tests failed.

The final native pass includes reduced motion, rapid overview replacement
and return to the normal desktop. Each Stage instance carries an ownership
token, so a previous popup's late destructor cannot restore the replacement's
preview. The narrow quick-settings scrollbar has a separate gutter.

### Validation coverage

- Scrolling was exercised with twenty real nested windows; Stage used four live tiling windows.
  Parser and QML tests cover hidden columns, minimized windows, inactive
  tabs, deletion, keyboard selection and all three placement modes.
- Stage's compositor transform was captured from the visible nested window,
  including a portrait output. A headless capture separately verified the
  map fallback. Tests cover full-screen geometry, selection versus activation,
  asynchronous begin/hide races and compositor-client lifetime.
- The vertical settings panel was checked at 800×600 and the wide shelf at
  1440×900. Connection rows, calendar and all three materials were captured.
  The shelf test preserves control instances and values while reflowing.
- Media was exercised with a controlled MPRIS player and a real CAVA process.
  Provider tests cover visibility/playback gating and sanitized samples.
- Appearance tests cover atomic persistence and import/export, invalid input,
  layout/font preservation across presets and widget reordering. The editor
  click test covers the saved result and immediate UI update. Live font and
  widget commands keep the settings panel open.
- Source-watcher tests distinguish appearance saves, identical atomic source
  replacement, subsequent direct edits and changed atomic source replacement.

The nested session does not verify physical Wi-Fi/Bluetooth connections,
real audio output or backlight changes, host power actions, session locking,
seat-owned authentication or physical gesture input. Their existing service
contracts and UI behavior have automated coverage. No host power action was
invoked. Calendar appointments and weather remain separate service features;
the redesign does not supply fictional data for them.

## Historical spectrum implementation (mockups-v2)

The following record describes the preceding design. Its visual rules are
superseded by `05-visual-identity.md` and `mockups-v3/`.

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

## Historical follow-ups

See `02-gap-analysis.md`. The first three items there (compositor-drawn
chrome packs, touchpad gesture progress, bundled faces) are the ones that
extend the identity; the surface gaps are ordinary feature work.

## Keeping this record honest

- A phase is not done until its unit tests, the full suite and a live capture
  agree. A green suite alone has been wrong twice in this work (a stale test
  binary, a stale effect binary).
- When a scripted edit lands, check `git status` before building. One batch
  of effect edits was accepted by the tooling and never reached the tree.
- For that historical implementation, `mockups-v2/` was the reference. If the lived design
  deviates, update them or note the deviation in their README.
