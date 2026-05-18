<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Scroll mode — niri-style scrollable tiling

Planning document. Status: **Phases 0 ✅, 1 ✅, 2 ✅ and 3 ✅ complete** — the scroll
engine, geometry resolver, `Mode::Scroll` routing, daemon geometry pipeline, the full
window lifecycle (open/close/focus, minimize, screen hotplug & geometry change,
fixed-size windows) and navigation (viewport fit/centered scrolling, consume/expel
shortcuts, drag-to-reorder, sticky-window exclusion) are implemented, built, and
tested — the full `ctest` suite is green. Phase 0 detail in
[`scroll-mode-phase0-findings.md`](scroll-mode-phase0-findings.md). Next: Phase 4.

## 1. Goal

Add a third placement mode to PlasmaZones — alongside Snapping and Autotile — that
implements **scrollable tiling** in the style of the [niri](https://github.com/YaLTeR/niri)
Wayland compositor: an unbounded horizontal strip of columns, each column a vertical
stack of windows, with a viewport that scrolls to keep the focused window visible.

The defining property to preserve:

> **Opening a new window inserts a new column and resizes nothing.** Existing windows
> keep their exact size. This is what makes the mode *niri-like* rather than just
> another tiling algorithm.

## 2. The niri model

niri's layout is a **flat, two-level list** — never a tree:

```
strip            ordered list of columns (unbounded width)
  └ column       ordered list of tiles (height bounded to screen)
      └ tile     one window
```

Key rules, all carried over verbatim:

- **Insert, don't reflow.** New window → new column to the right of the focused one.
  Neighbours are untouched. `consume-window-into-column` instead adds it as a tile in
  the current column.
- **Intent vs. resolved geometry are separate.** Store column *width* and tile *height*
  as intent enums (`Proportion(f)` / `Fixed(px)` / `Preset(idx)`); recompute pixel rects
  on relayout. Never store pixels as the source of truth.
- **The viewport scroll position (`scrollX`) is an absolute strip coordinate** — the
  strip-x that maps to the working area's inner-left edge. The daemon recomputes it on
  every relayout (`computeViewportScroll`, `fit` or `centered`) so the focused column
  stays on-screen; `fit` leaves an already-visible column untouched, which is what stops
  the focused window jumping when columns to its left are added/removed/resized.
- **Fullscreen / maximize / tabbed are column states**, not separate modes — they stay
  first-class participants in the strip.

niri reference source: `src/layout/mod.rs`, `src/layout/scrolling.rs`.

## 3. Karousel — the reference implementation

[Karousel](https://github.com/peterfajdiga/karousel) already implements scrollable
tiling on KDE as a **KWin script**. It proves the concept is feasible *without being the
compositor*, and its source/issue tracker is a ready-made bug list.

What Karousel establishes:

- A **virtual strip coordinate space** works: columns carry an absolute unbounded
  `gridX`; screen position = `gridX − scrollX + areaX`.
- Off-screen windows are **positioned, not hidden** — they stay mapped and live.
- Smooth scrolling requires a *separate* companion KWin effect
  (`kwin4_effect_geometry_change`), because a KWin script cannot animate.

Where PlasmaZones starts **ahead of Karousel** — because it is a KWin *effect* + daemon,
not a script:

| Karousel limitation | PlasmaZones advantage |
| --- | --- |
| Can't animate; needs a second companion effect | PlasmaZones *is* an effect (`kwin-effect/`, `windowanimator`) — animate in-process |
| No multi-monitor support | `ScreenManager` + per-screen assignment cascade + `ScreenModeRouter` already exist |
| No persistence (re-derives on restart) | JSON persistence + the `m_lastAutotileOrders` pattern already exist |
| Boolean window flags | `IPlacementEngine` abstraction gives a clean home for a real state machine |
| Applies geometry to every window on every change | Only *apply geometry* to the visible range (see §4.1 / §6) |

Karousel mistakes **not** to repeat: brittle `resourceClass` blocklist (use proper
window-type classification + app rules); shortcut defaults colliding with stock KDE;
reacting to every `windowAdded` including transient/clipboard helpers; unhandled
"last visible window closed" focus edge cases.

## 4. KWin effect-level analysis

Karousel hits three problems as a *script*. PlasmaZones, as an *effect*, can reach the
internal C++ `KWin::Window` API and mitigate all three substantially better. Line
numbers below are indicative (KWin 6.x) and will drift between versions; the
classes/methods are stable.

### 4.1 Off-screen "stuck window"

The "KWin drops off-screen geometry writes" folklore is **wrong** — KWin does not clamp
off-screen moves. Real causes:

- `frameGeometry =` always routes through `Window::moveResize()` with
  `MoveResizeMode::MoveResize`. When a size change is involved, the *position* is
  bundled into a Wayland configure event and deferred until the client acks + commits.
  A slow/coalescing client can lose it.
- `Window::checkWorkspacePosition()` re-clamps on monitor hotplug/rearrange — the
  genuine stuck-off-screen bug ([KDE #494001](https://bugs.kde.org/show_bug.cgi?id=494001)).

**Fix — pure `move()`:** an effect can call `effectWindow->window()->move(pos)`, which
takes the `MoveResizeMode::Move` branch — cancels queued configures and applies geometry
**synchronously**, no client round-trip. Scripts cannot express a pure move. Scrolling
is pure translation (no resize), so the most frequent operation gets the reliable path.
Column-width changes still resize → async configure, but those are infrequent.

**Hybrid geometry model — viable, but with one UNVERIFIED assumption:**

- Real geometry for windows in/near the viewport (±1 column).
- Paint-time `WindowPaintData` translation for fully-off-screen windows.
- **Hard constraint A — input does NOT follow paint transforms.** `Window::hitTest()`
  uses real geometry only. Paint-transform is safe *only* for fully-off-screen windows
  (un-clickable anyway) — never for a partially-visible one.
- **Hard constraint B — UNVERIFIED, the biggest open risk.** A KWin effect's
  `paintWindow()` is only called for windows the scene decides to paint. A window whose
  real geometry is *fully off-screen* is likely **culled** — `paintWindow()` never
  fires, so no transform can pull it into view. A paint-transform can move a window
  *out of* view (it starts on-screen, gets painted) but cannot bring an off-screen
  window *in*. **Phase 0 must verify whether KWin calls `paintWindow()` for windows
  with partially- vs. fully-off-screen real geometry.** If fully-off-screen windows are
  culled, the model below is mandatory.
- **Animation ordering (corrected for constraint B).** Before a scroll animation, set
  real geometry to an on-screen position for *every* window that will be visible at any
  frame of the animation — including incoming windows (commit their final geometry
  *first*) and outgoing ones. Paint-transforms then only *interpolate* between committed
  positions. Windows that stay fully off-screen for the entire animation get no
  transform. After the animation settles, re-park genuinely off-screen windows via
  `move()`. (Karousel's `commit-at-animation-end` order is wrong for incoming windows;
  do not copy it.)

This structurally eliminates the stuck bug: real geometry is never set to an extreme
off-screen value. The Karousel `place()` poke-hack is **not needed**.

Off-screen windows are *parked* via `move()` at a valid position just outside the
nearest output — distinct from the §4.4 "drop into a KWin tile" fallback used on mode
exit.

### 4.2 Self-write vs. external-write disambiguation

Because `Window::move()` is synchronous, `frameGeometryChanged` fires *inside* the
`move()` call. A re-entrancy guard is therefore deterministic, not heuristic:

```cpp
m_applyingOwnMove = true;
window->move(target);     // frameGeometryChanged fires here, before move() returns
m_applyingOwnMove = false;
```

Any `frameGeometryChanged` seen with the flag false is definitely external. Tiler-
initiated *resizes* are still async → keep a small per-window "pending expected
geometry" set. Match against it with a **size tolerance**: a client may legitimately ack
a different size than requested (min-size / aspect-ratio / increment constraints), so an
exact-match check would misclassify a satisfied resize as an external change. Treat
"close to a pending size" as own-write and reconcile the column to the actual size.
`isInteractiveMove()` / gravity / anchor (effect-only) cleanly separate user drags.

### 4.3 Focus-set failures

Scripts call `activateWindow()` with `force=false` → gated by focus-stealing-prevention
and "not yet mapped" checks. An effect can call
`Workspace::activateWindow(window, /*force=*/true)` directly — `force=true` bypasses FSP.
For brand-new windows, replace Karousel's retry loop with a single event-driven
activation on the `windowShown` signal.

**Residual risk:** `force=true` is a genuine focus-steal — only for user-initiated
actions (focus-follows-scroll, "focus window N"), never background re-tiling. Under a
focus-follows-mouse policy the cursor will sit over a different window after a scroll and
KWin will re-focus it on the next pointer event, fighting the engine — see §8.

### 4.4 KWin built-in tiling — evaluated and rejected as the engine

`KWin::Tile` / `TileManager` are per-output and within-screen only; they cannot model a
canvas wider than the screen. Useful at most as a fallback resting place when the user
*exits* scroll mode (windows dropped into a real KWin tile layout). Build the strip model
independently.

## 5. Recommended architecture

A third engine, **`ScrollEngine : IPlacementEngine`**, alongside `SnapEngine` and
`AutotileEngine`. `ScreenModeRouter` + `AssignmentEntry::Mode` already dispatch
polymorphically — add `Mode::Scroll`.

**Not** a `TilingAlgorithm` inside `AutotileEngine`: that contract is stateless
full-reflow (`calculateZones(windowCount) -> QVector<QRect>`), which is fundamentally
incompatible with niri's "opening a window resizes nothing" and has nowhere to hold
per-column widths, per-tile heights, view offset, or focused indices.

**`IPlacementEngine` needs six new methods — zero blast radius** (confirmed by Phase 0).
The six niri operations with no current equivalent — `consume`/`expel` window, set/cycle
column width, set/cycle window height, scroll-the-viewport, center-column — are added as
**optional virtuals with no-op defaults**, following the existing autotile-only group
(`increaseMasterRatio`, `focusMaster`, …). `SnapEngine` and `AutotileEngine` inherit the
defaults and need no source changes. Basic navigation (focus/move/swap in four
directions) already maps onto the existing direction-string methods. See the findings
doc §2.

Suggested home: a new `libs/phosphor-scroll-engine` library (LGPL-2.1-or-later, matching
`phosphor-tile-engine`), holding the pure-logic strip model unit-tested in isolation,
parameterised over the window type like niri's `Layout<W>`.

Persistent per-context (screen / desktop / activity) state:

```
ScrollScreenState
  columns: QVector<Column>
  activeColumnIdx
  scrollX               // absolute strip-x at the viewport's inner-left edge
Column
  tiles: QVector<Tile>  // each tile = one windowId
  activeTileIdx
  width: ColumnWidth    // intent: Proportion(f) | Fixed(px) | Preset(idx)
  presetWidthIdx
Tile
  windowId
  height: WindowHeight  // intent: Auto{weight} | Fixed | Preset
```

Geometry application = the **hybrid** of §4.1, built around synchronous `Window::move()`
for commits + `WindowPaintData` transforms for animation. *Computing* the layout always
covers the whole strip (off-screen columns included, so they can be parked and
animated); *applying* real geometry is limited to the visible range ±1 column.

## 6. Phased implementation plan

### Phase 0 — Feasibility verification

Static code review **complete** — results in
[`scroll-mode-phase0-findings.md`](scroll-mode-phase0-findings.md). Summary:

1. **`IPlacementEngine` contract review — DONE.** Required surface satisfiable; six niri
   ops added as zero-impact optional virtuals; `IPlacementState` and per-context state
   already fit.
2. **Self-write disambiguation — DONE.** Already solved in-tree (`m_inDaemonGeometryApply`).
3. **Focus — DONE.** Path exists; needs a `force` activation parameter on
   `ICompositorBridge`. focus-follows-mouse already handled.
4. **Paint-transform culling — RESOLVED by code review.** PlasmaZones' paint pipeline
   already renders windows outside their frame geometry for every snap/autotile
   animation (`PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS` + `setTransformed()` +
   `WindowAnimator`). Scroll mode reuses `WindowAnimator` directly. See findings §3.

**Phase 0 conclusion: no blockers.** Ready for Phase 1.

### Phase 1 — Strip data model + engine skeleton — ✅ COMPLETE

- ✅ `libs/phosphor-scroll-engine`: `ScrollScreenState` / `Column` / `Tile` model +
  `resolveScrollLayout()` geometry resolver. Pure logic, no KWin, with a dedicated
  unit-test suite.
- ✅ `ScrollEngine : IPlacementEngine` (extends `PlacementEngineBase`); the four niri
  operations added as optional `IPlacementEngine` virtuals.
- ✅ `Mode::Scroll` in `AssignmentEntry`; `LayoutId` `scroll:` helpers; `ScreenModeRouter`
  dispatch to a third engine; per-mode disable-list settings keys.
- ✅ Daemon integration: the engine factory constructs `ScrollEngine`;
  `updateScrollScreens()` resolves scroll-mode screens; `onScrollPlacementChanged()`
  resolves geometry and pushes it to the effect via `applyGeometriesBatch`.

Re-entrancy: the geometry pipeline rides the effect's existing
`slotApplyGeometriesBatch` path, which already raises the `m_inDaemonGeometryApply`
guard — no separate guard needed. `ScrollEngine` is geometry-agnostic, so it issues
no compositor moves of its own; the `ICompositorBridge::move()` / forced-activation
additions sketched in the Phase 0 findings are deferred — they are an optimisation for
a future direct-bridge path, not required by the implemented effect-batch pipeline.

Deferred to later phases (were surveyed under "M3c" but belong elsewhere per this
plan's phase boundaries): the niri-op **keyboard shortcuts** → Phase 3 ("wire to
`ShortcutManager`"); the **`ScrollAdaptor`** D-Bus interface for KCM selection →
Phase 5 ("Settings, UI").

### Phase 2 — Window lifecycle — ✅ COMPLETE

- ✅ **Open / close / focus** (M1) — windows on a scroll-mode screen are reported to
  the `ScrollEngine` over `org.plasmazones.Scroll`; the engine adds/removes columns and
  tracks the focused column. The effect-side `ScrollHandler` owns this surface,
  mirroring `AutotileHandler` — neither handler knows the other's mode.
- ✅ **Minimize / unminimize** (M2) — Karousel's `TiledMinimized`: a minimized window
  keeps its slot in the strip but drops out of the resolved layout; a fully-minimized
  column collapses with no gap and reappears in place on restore. Minimizing the focused
  window hands focus to the first still-visible window in strip order, so the viewport
  never anchors on a hidden window.
- ✅ **Screen hotplug / geometry change** (M3) — the daemon re-resolves every scroll
  strip on a debounced geometry change; `ScrollHandler` re-homes windows that move
  between monitors or virtual screens; the scroll-screen-set diff adopts or releases
  windows when a screen enters or leaves scroll mode at runtime.
- ✅ **App-initiated resize** (M4) — a window that drifts from its resolved tile
  geometry is snapped back, debounced to coalesce noisy resize streams.
- ✅ **Fixed-size / non-resizable windows** (M4) — MVP fallback: the window keeps its
  constrained size, centered within its tile slot (no floating layer yet — see §7).
- ✅ **Filtering** — transient / clipboard / dock / popup / menu / tooltip / modal /
  keep-above / minimized / off-desktop / undersized windows are all rejected by
  `PlasmaZonesEffect::isEligibleForTilingNotify`, shared with autotile.
- ✅ **Geometry application** — resolved rects ride the effect's existing
  `applyGeometriesBatch` path. No animation yet — snap.

**Virtual desktop / activity switch**: handled structurally — `ScrollEngine` keys one
strip per (screen, desktop, activity) and `updateScrollScreens()` sets the engine's
context on every switch, so each desktop/activity shows its own strip. The narrower
decision for a window *pinned to all desktops / multiple activities* (force-collapse to
the current context vs. exclude) is deferred — sticky-window handling is carried forward
with the Phase 3 navigation work.

### Phase 3 — Navigation & niri command vocabulary — ✅ COMPLETE

- ✅ **Viewport scrolling** (M1) — the strip scrolls to keep the focused column
  on-screen. `computeViewportScroll` resolves an absolute scroll position per
  `ScrollViewportMode`: `Fit` (minimum scroll — leaves an already-visible column
  untouched) or `Centered`. Fit is the default; the mode becomes a user setting in
  Phase 5.
- ✅ **Focus / move / swap navigation** — already routed: the daemon dispatches every
  navigation shortcut through `ScreenModeRouter::engineFor()` and `ScrollEngine`
  implements the `IPlacementEngine` contract, so focus left/right/up/down and
  move-column / move-window work in scroll mode with the existing shortcuts.
- ✅ **`consume` / `expel`** (M2) — new `Meta+Alt+I` / `Meta+Alt+O` shortcuts pull the
  next column's window into the focused column / push the focused window out into its
  own column. The snap/autotile engines inherit the no-op `IPlacementEngine` default,
  so the shortcuts are harmlessly absorbed on their screens. Settings-UI exposure is
  Phase 5.
- ✅ **Interactive drag of a tiled window** (M3) — drag-to-reorder: a dragged window
  keeps its tile slot during the move and, on release, its column is reordered to the
  strip slot nearest the drop point, then the strip re-resolves and snaps it in.
- ✅ **Sticky windows** (M4) — a window pinned to all desktops is never tiled into a
  strip (it floats); pinning / un-pinning at runtime drops it from / re-adds it to the
  strip.

### Phase 4 — Widths/heights & viewport polish

- Preset column-width cycling, `set-column-width`, full-width toggle; preset window
  heights.
- Smooth scroll animation via paint transforms, using the §4.1 corrected ordering;
  commit real geometry for visible windows *before* animating, re-park after.
- `center-focused-column` modes.

### Phase 5 — Settings, UI, persistence

- `ConfigDefaults` keys: default/preset column widths, preset heights, gaps, centering
  mode, animation. Per-screen overrides reuse the existing mechanism.
- Settings UI + layout-picker entry ("Scrollable").
- Persist strip order across daemon restart (reuse the `m_lastAutotileOrders` pattern).

## 7. MVP scope cuts

Deferred past v1 to keep it shippable:

- **Dynamic vertical workspaces** — niri's always-empty-trailing / auto-collapse model
  collides with KDE's static virtual desktops. MVP: one strip per existing KDE virtual
  desktop. Do not fake niri workspaces.
- **Full floating layer** — deferred. Note this leaves fixed-size / non-resizable windows
  without a dedicated layer; the Phase 2 fallback (native size, centered in column slot)
  is the MVP answer and must ship, since such windows *will* occur.
- **Tabbed columns**, touchpad **scroll gestures**, and the **overview** — all post-MVP.

## 8. Open questions

1. Confirm mapping a strip onto each existing KDE virtual desktop (rather than
   replicating niri's dynamic workspaces) is acceptable.
2. Confirm the new `phosphor-scroll-engine` library + LGPL-2.1-or-later licensing.
3. Phase 0 item 1: does KWin paint a window whose real geometry is fully off-screen? The
   §4.1 animation model depends on the answer.
4. Phase 0 item 4: does `IPlacementEngine` need new virtual methods, and is the impact on
   the other two engines acceptable (§5)?
5. **focus-follows-mouse interaction** — under that focus policy, KWin re-focuses
   whatever window is under the cursor after a scroll, fighting engine-driven focus.
   Decide: warp the cursor with the focused window, temporarily suppress the policy
   during engine actions, or document focus-follows-mouse as unsupported with scroll mode.
6. **XWayland windows** — they can behave differently on geometry/configure round-trips;
   confirm the §4.1 model holds for them, or scope them out of scroll mode.

## 9. References

- niri — `github.com/YaLTeR/niri`; `src/layout/mod.rs`, `src/layout/scrolling.rs`;
  wiki pages Configuration:Layout, Workspaces, Design-Principles.
- Karousel — `github.com/peterfajdiga/karousel`; issues #19, #136, #146, #153, #161,
  #163, #166, #167. (Internal file paths cited during research were approximate — verify
  against the current repo layout before relying on them.)
- KWin source — `invent.kde.org/plasma/kwin`; `src/window.cpp`, `src/xdgshellwindow.cpp`,
  `src/waylandwindow.cpp`, `src/activation.cpp`, `src/effect/effect.h`.
- [KDE #494001 — windows stuck offscreen](https://bugs.kde.org/show_bug.cgi?id=494001).
- Vlad Zahorodnii, "Geometry handling in KWin/Wayland".
- Kai Uwe Broulik, "On Window Activation".
