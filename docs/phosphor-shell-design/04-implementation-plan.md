<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 04: Implementation Record

## Notification migration (mockups-v3)

The approved notification study now drives the native surfaces. The retained
controller preserves app identity, pictures, actions, per-entry unread state,
and bounded history. Clearing individual entries, app groups or the inbox can
be undone without reviving actions belonging to a closed notification. D-Bus
replacements update the existing card; expiry keeps its saved content. Scripted
`notify.send` calls use the same ingestion path.

The shared rich card appears in the grouped 444 px inbox and the 396 px arrival
stack below the status area. Long messages expand, pictures preserve aspect
ratio, and supported applications receive private inline replies. Reading or
typing pauses expiry. The inbox includes urgent entries, All/Unread filters,
mark-as-read, DND, clear/undo and empty states. Appearance controls app/time
grouping and preview privacy across both surfaces. Opening the center, DND and
session locking suppress arrival popups. The default bar includes the inbox.

Data/service phase: `fc2c9bda0`. Native surface phase: this commit. Native KWin
checks cover grouped history and real D-Bus pictures, expansion and reply input;
unit coverage includes retention, replacement, unread bounds, undo with newer
arrivals, action validation, expiry pause, rich rendering and preview privacy.

## Mockup fidelity correction (mockups-v3)

The first six-phase implementation did not reproduce the approved mockups.
The correction below uses the HTML studies at 1440×900 as the visual reference
and checks the rendered shell in an isolated KWin session.

| Phase | Commit | Result |
|---|---|---|
| A. Desktop and bar | `7ec9926ae` | Floating bar geometry, shared palette, wallpaper, rail, material and default widget ordering. |
| B. Quick settings | `0c3b8e7fd` | Navigator panel and Stage shelf, continuous connection rows, sliders, service-backed controls and CAVA media visualization. |
| C. Workspace navigation | `009f06964` | Distinct Navigator and Stage layouts, complete native window catalogue, bounded scrolling, window actions and safe popup teardown during reload. |
| D. Date and time | `3ee723de9` | Calendar typography and layout, local timezone, real dates, optional agenda provider, keyboard navigation and rounded compositor blur. |
| E. Launcher | `a5c63d5f6` | Navigator result list and Stage pinned-app shelf, native window search, activation and saved pins. |
| F. Appearance and feedback | `88252edf9` | Appearance controls, preserved popup position and scroll state through reload, compact OSD and toast styling. |
| G. Native desktop windows | `1071fc53c` | KDecoration3 titlebars, shared stable window colors, palette and radius updates, real placement gaps and reversible desktop styling. |
| H. Final visual and interaction checks | `a1e91f9b8` | Readable Stage titlebars over live content, rounded preview silhouettes, immediate popup keyboard input and focus restoration on dismissal. |
| I. Workspace transitions | This commit | Native snapshots populate inactive cards, cards rebind after desktop switches, and Stage aligns scrolling previews with native frames. The compositor clips content to the preview and waits for an existing desktop transition before opening it. |

### Validation

`cmake --build build --parallel 8` succeeds. The completed tree passes
`ctest --test-dir build --output-on-failure --parallel 8`: 540 test targets,
539 passed and the existing `test_surface_decoration_orientation` skipped.

Native checks include both presentations and all three presets, the 800×600
control center and calendar, Stage selection versus activation, and twenty
real scrolling windows. Home/End and Enter reach the first and last windows
without a pointer click. Esc returns focus to the app, and clicking another
window dismisses Navigator. The bar miniature remains bounded as the strip
grows. Repeated overview replacement and appearance reloads leave the shell
and compositor running.

The native window decoration is built with the shell under
`org.kde.kdecoration3`. Appearance's “Match desktop windows” option applies
the frame and spacing together. A journal preserves the prior settings;
disabling the option or a clean shutdown restores only values still owned
by the shell. Per-window colors are shared with the bar and workspace maps.
Stage keeps the wallpaper in place and fits each complete app view below
its full-size titlebar without changing the real window's desktop geometry.

Third-party app content and live service data naturally differ from the
illustrative mockup data. Calendar appointments require an agenda provider.
Media visualization was checked with a controlled MPRIS player and a real
CAVA process. No host power action was invoked. The nested compositor does
not verify physical connectivity, backlight or audio changes, session
locking, seat-owned authentication, or physical gestures; their service and
UI contracts retain automated coverage.

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

### v3 lock screen (September 2026)

- `2617e2aff`: replace the placement-map outlines and region finder with the
  approved abstract panes, clock and unlock card. Add persistent lock layout,
  media privacy and notification-count preferences.
- `eb02114a0`: connect the shared PAM controller, authenticated account name,
  compositor Caps Lock and keyboard layout, battery, notification count, MPRIS
  and session actions. Preserve the lock-before-sleep and release handshakes
  and the `shell.phosphor.lock` surface-pack slot.
- Native verification: both compositions and all three presets at 1440×900,
  plus an 800×600 output, optional media, keyboard navigation, confirmation,
  masked input, retry, busy and release states. The short layout keeps the
  whole unlock card visible and scrolls optional content into view on focus.

`scripts/nested-shell/lock-preview.sh run` uses production components with
fixture auth/media/power services on an existing nested session. Its config
and IPC socket are separate from the ordinary shell. Use the same script
with `call preview.result --arg name=error`, `success` or `idle` to drive the
outcome. Success hides the preview windows after the exit; idle shows them
again. The actual session-lock protocol is unavailable in virtual KWin, so
PAM and compositor-lock lifecycle validation comes from the service tests.

The final build passed. The full suite ran 541 cases: 540 passed and the
existing surface-decoration orientation test was skipped. Real nested
keyboard events confirmed Caps Lock, typing, submit, retry focus, and the
power confirmation flow without invoking host session actions.

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
