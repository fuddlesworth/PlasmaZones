<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Scroll mode — niri-style scrollable tiling

Planning document. Status: **pre-implementation research complete, Phase 0 not started.**

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
- **The view offset is relative to the focused column**, not an absolute strip
  coordinate. This is what stops the focused window drifting when columns to its left
  are added/removed/resized.
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

**`IPlacementEngine` will probably need extending.** niri operations — `consume`/`expel`
window, `set-column-width`, scroll-the-viewport — likely have no equivalent on the
current interface. New virtual methods on `IPlacementEngine` ripple into `SnapEngine` and
`AutotileEngine` (which must at least no-op them). Phase 0 assesses the gap and the
blast radius before any interface change is committed. Scroll-only commands that make no
sense for the other engines should live on `ScrollEngine`'s concrete type and be reached
after a `dynamic_cast` / mode check, not forced onto the shared interface.

Suggested home: a new `libs/phosphor-scroll-engine` library (LGPL-2.1-or-later, matching
`phosphor-tile-engine`), holding the pure-logic strip model unit-tested in isolation,
parameterised over the window type like niri's `Layout<W>`.

Persistent per-context (screen / desktop / activity) state:

```
ScrollScreenState
  columns: QVector<Column>
  activeColumnIdx
  viewOffset            // relative to active column — NOT absolute
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

### Phase 0 — Feasibility verification (~3 days)

Ordered by risk — stop and reconsider if item 1 fails.

1. **Paint-transform culling test (highest risk).** In a throwaway effect build,
   determine whether KWin calls `paintWindow()` for a window whose real geometry is
   (a) partially off-screen, (b) fully off-screen. The answer dictates the §4.1
   animation model; if fully-off-screen windows are culled, the corrected ordering in
   §4.1 is mandatory and unavoidable.
2. **Synchronous `move()` test.** Confirm `kwin-effect/` can call
   `effectWindow->window()->move()` and that it applies synchronously in the target
   KWin version.
3. **Input dead-zone test.** Confirm a paint-transformed window's clicks land at its
   real geometry, and that this only affects fully-off-screen windows.
4. **`IPlacementEngine` contract review.** Read the actual `IPlacementEngine` header;
   list which niri operations have no method, and assess the impact of adding them on
   `SnapEngine` / `AutotileEngine` (see §5).

### Phase 1 — Strip data model + engine skeleton

- New `libs/phosphor-scroll-engine`: `ScrollScreenState` / `Column` / `Tile` model +
  relayout logic. Pure logic, no KWin — fully unit-tested.
- `ScrollEngine : IPlacementEngine` skeleton; `Mode::Scroll` in `AssignmentEntry`;
  `ScreenModeRouter` dispatch; any agreed `IPlacementEngine` additions.
- Re-entrancy guard for engine-initiated moves + pending-resize set with size tolerance
  (§4.2).

### Phase 2 — Window lifecycle

- **Open** → new column on the window's output's strip; **close** → remove column/tile,
  explicitly handling "last visible window closed" focus selection.
- **Focus** → update active indices + recompute view offset.
- **App-initiated resize** → relayout the affected column; debounce noisy resize streams.
- **Minimize / unminimize** → decide and document the rule: a minimized tiled window is
  removed from the visible strip but its slot/order is remembered, and restored on
  unminimize (Karousel's `TiledMinimized` state).
- **Fixed-size / non-resizable windows** → MVP fallback: keep the window at its native
  size, centered within its column slot (no full floating layer yet — see §7).
- **Virtual desktop / activity switch** → activate that context's strip; a window pinned
  to all desktops or multiple activities is force-collapsed to the current context (as
  Karousel does) or excluded — decide and document.
- **Screen hotplug / geometry change** → re-resolve outputs, reassign orphaned strips,
  re-assert geometry on `outputChanged` / rearrange signals (§4.1). This area is already
  fragile in the codebase — treat as a first-class task, not an afterthought.
- Aggressive early filtering of transient / clipboard / dock / popup windows.
- Apply resolved geometry through the effect via `move()`. No animation yet — snap.

### Phase 3 — Navigation & niri command vocabulary

- Focus left/right (columns), up/down (tiles).
- `consume` / `expel` window into/out of column; `move-column`; `move-window-up/down`.
- **Interactive drag of a tiled window** — untile-on-drag or live column reorder; reuse
  `kwin-effect/dragtracker.cpp`.
- View-offset computation: `fit` (minimum scroll) and `centered`.
- Wire to `ShortcutManager`; pick defaults that do not collide with stock KDE.

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
