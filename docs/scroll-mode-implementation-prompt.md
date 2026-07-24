<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# One-shot implementation prompt — Scrolling mode (niri-style scrollable tiling)

> This file is a **prompt** to hand to a fresh Claude Code session so it can implement the
> feature in one pass. It is self-contained: it carries the behavioral spec, the verified
> in-repo integration seams, the repo conventions, and the phasing. Paste the section below
> the line as the task. Everything references the **current** code (verified by reading source,
> not any older planning note). An earlier `docs/scroll-mode-plan.md` design doc was **obsolete and
> has been removed**; this file supersedes it.

---

## PROMPT STARTS HERE

You are implementing a major new feature in **PlasmaZones** (KDE Plasma window tiler; Qt6/KF6,
C++20, QML/Kirigami, Wayland-only, daemon + KWin effect). Add a **third placement mode —
"Scrolling"** — alongside the existing **Snapping** and **Autotile** modes, reproducing the
scrollable-tiling behavior of the **niri** compositor as closely as KDE/KWin allows. This is
scrolling only; niri's overview/gestures are explicitly out of scope for this release.

Before writing code, **read the current source for every seam named below and confirm it still
matches** — this repo moves fast; treat any drift as reality and adapt. Follow `CLAUDE.md`
exactly (naming, string-literal rules, licensing/SPDX, file-size ≤1000 lines with 1150 hard
ceiling, plain-prose user-facing text, "solve the root cause, no TODOs/for-now hacks",
run tests + verify build before finishing). Do not commit or push unless asked.

### 0. The one invariant that defines the mode

> **Opening a new window inserts a new column and resizes nothing.** Existing windows keep
> their exact geometry. The viewport scrolls if needed to keep focus visible.

This is what makes it niri-like rather than "just another tiling algorithm". Every design choice
below serves it. If a decision would force existing windows to reflow on open, it is wrong.

### 1. Behavioral specification (niri model, distilled)

**Structure — a flat two-level list, never a tree:**
```
strip     ordered list of columns, unbounded width, one strip per (screen, virtual-desktop)
 └ column ordered list of tiles, stacked vertically, height bounded to the screen work area
    └ tile one window
```

**Viewport / camera.** The screen work area is a fixed window onto an infinite horizontal strip.
Scrolling pans the view; columns do not move relative to each other. The strip has a hard left
edge (first column) and is unbounded on the right. **Store the view offset relative to the
focused column, not as an absolute strip coordinate** — this is what keeps the focused window
from drifting when columns to its left are added/removed/resized.

**Intent vs. resolved geometry are separate.** Store column *width* and tile *height* as intent
values (`Proportion(f)` | `Fixed(px)` | `Preset(idx)`); recompute pixel rects on every relayout
against the current work area. Never treat pixels as the source of truth.

**center-focused-column** (persistent enum, the single most characteristic niri knob):
- `never` (default): focusing an off-screen column scrolls the minimum amount to pin it to the
  edge it entered from; it does not center.
- `always`: the focused column is always centered in the view.
- `on-overflow`: center only when the focused column plus the previously focused one cannot both
  fit on-screen.

Also support **always-center-single-column** (center a lone column instead of left-pinning).

**Column widths.** Preset list defaulting to `1/3, 1/2, 2/3` of the work area (proportions
account for gaps). Operations: cycle-preset (forward/back), `set-column-width` accepting a
proportion, a fixed px, or a `+N%`/`-N%` delta, `maximize-column` (full work-area width, still
tiled), and `expand-column-to-available-width` (grow into leftover on-screen space).
`default-column-width` sets the width new columns open at (proportion, fixed, or "client
decides"); default `proportion 0.5`.

**Window heights within a column.** Preset list defaulting to `1/3, 1/2, 2/3`; `set-window-height`
(proportion/fixed/±%); `reset-window-height` (back to even auto-split). Tiles in a single-tile
column fill the column height.

**Operation vocabulary** (implement all; bind defaults in §7):
- Focus: column-left/right, window-up/down (within column), column-first/last.
- Move: column-left/right, window-up/down (reorder within column), column-to-first/last.
- Consume/expel: `consume-window-into-column` (pull next column's window into current),
  `expel-window-from-column` (push focused tile out into its own adjacent column), and the
  combined `consume-or-expel-window-left/right`.
- Center: `center-column`.
- Tabbed display: `toggle-column-tabbed-display` (switch the focused column between normal
  vertical-stack and tabbed presentation).
- Width/height: the width and height operations listed above.
- Cross-context: move-column-to-monitor-left/right, move-column-to-(virtual-desktop N).
- Float: toggle focused window floating (pulls it out of the strip). **Reuse the existing
  PlasmaZones floating support** (`toggleWindowFloat`/`setWindowFloat` + float handoff) — do not
  build a new floating layer; a floated window leaves the strip and its column closes up.

**Window lifecycle rules:**
- **Open** → new column immediately to the right of the focused column on that window's output's
  strip; nothing else resizes; view scrolls only if needed.
- **Close** → remove the tile/column; the strip closes up; the view stays anchored so neighbors
  don't jump; handle "last visible window closed" focus selection explicitly.
- **Focus change** → update active column/tile indices and recompute the view offset per
  `center-focused-column`.
- **App-initiated resize** → relayout only the affected column; reconcile to the size the client
  actually acked (clients may honor min-size/aspect/increment constraints); debounce noisy resize
  streams.
- **Minimize / unminimize** → removed from the visible strip but its slot/order is remembered and
  restored on unminimize.
- **Fixed-size / non-resizable window** → float it. PlasmaZones already has first-class floating;
  the scroll engine reuses the **existing** float mechanism (`toggleWindowFloat`/`setWindowFloat`
  and the float-handoff path) exactly as the snap and autotile engines do. A window that cannot
  honor its column slot is taken out of the strip and floated at its native size rather than being
  forced to tile.
- **Fullscreen** → a column state, not a separate mode; the window covers the output and remains a
  first-class strip participant on exit.
- **Tabbed columns (v1 feature).** A column has a `display` state: `normal` (windows split the
  column height vertically, all visible) or `tabbed` (only the active tile shows, at full column
  height; the other tiles are hidden and represented by a tab-indicator strip). It is an alternate
  presentation of the same column, orthogonal to horizontal scrolling — the strip, widths, view
  offset, consume/expel, and move operations all behave identically regardless of `display`.
  `focus-window-up/down` and `move-window-up/down` cycle/reorder tabs in a tabbed column just as
  they do stacked tiles. New columns open in the mode set by `default-column-display`.
  **Effect-side work:** in tabbed mode the non-active tiles must be visually suppressed (hidden or
  parked off-canvas via the same `move()`/paint path used for off-screen columns — a hidden tile is
  un-clickable, so the paint-transform constraints in §3 do not bite), and a tab-strip indicator is
  rendered for the column. Keep the tab-strip render minimal and themed (Kirigami units/colors).
- **Virtual-desktop / activity switch** → activate that context's strip.
- **Screen hotplug / geometry change** → re-resolve outputs, reassign orphaned strips, and
  re-assert geometry on output-changed/rearrange signals. Treat this as a first-class task; this
  area is already delicate in the codebase.
- Aggressively filter transient/clipboard/dock/popup/utility windows out of the strip.

**Per-monitor & workspaces.** Each monitor has its own independent strip (do **not** copy
Karousel's single relocatable grid). **MVP scope cut:** map niri's dynamic vertical workspaces
onto KDE's existing static virtual desktops — one strip per (screen, virtual-desktop). Do not
fake niri's dynamic-workspace model. **Cut from v1:** touchpad scroll gestures and the overview.
**Floating is NOT a scope cut.** PlasmaZones already supports floating and the scroll engine reuses
it as-is (see the lifecycle rules above). **Tabbed columns ARE in v1** (see §1 tabbed-column rules,
the `display` field in §4, and the `default-column-display` setting in §5).

### 2. Where it slots into the CURRENT architecture (verified seams)

PlasmaZones is a **daemon (authoritative placement) ↔ KWin effect (actuator)** split connected by
D-Bus using `phosphor-protocol` types. Geometry is computed daemon-side as **absolute pixel
rects** and pushed to the effect, which owns all animation and painting. The scrolling engine is a
new daemon-side placement engine; the effect already has every actuator you need.

**The engine interface & base.**
- `libs/phosphor-engine/include/PhosphorEngine/IPlacementEngine.h` — the interface every engine
  implements. Required pure-virtuals include `isActiveOnScreen`, `windowOpened`, `windowClosed`,
  `windowFocused`, `toggleWindowFloat`, `setWindowFloat`, `focusInDirection`,
  `moveFocusedInDirection`, `swapFocusedInDirection`, `moveFocusedToPosition`, `rotateWindows`,
  `reapplyLayout`, `snapAllWindows`, `cycleFocus`, `pushToEmptyZone`, `restoreFocusedWindow`,
  `toggleFocusedFloat`, `saveState`, `loadState`, `stateForScreen`. Optional seams you will use:
  `capturePlacement`/`restorePlacement` (engine-agnostic persist/restore), `managedWindowOrder`/
  `setInitialWindowOrder`, `engineId()` + `handoffReceive`/`handoffRelease` (cross-engine window
  transfer), `onWindowResized`, the drag-insert-preview cluster, and the desktop/activity context
  setters.
- `libs/phosphor-engine/include/PhosphorEngine/PlacementEngineBase.h` — the QObject base that
  implements `IPlacementEngine` and carries signals `placementChanged`, `settingsPersistRequested`.
  **`ScrollEngine` derives from `PlacementEngineBase`.**

**The mode enum is already reserved — no enum change needed.**
- `libs/phosphor-zones/include/PhosphorZones/AssignmentEntry.h` — `enum Mode { Snapping=0,
  Autotile=1, Scrolling=2 }` already exists, with wire string `"scrolling"`
  (`modeToWireString`/`modeFromWireString`) and inclusion in `allModes()`. **Never renumber**
  (v3→v4 disable-list migration depends on the values). Assigning a screen/context to Scrolling
  already works through the Rules layer (`SetEngineMode{mode:"scrolling"}`) the moment the engine
  is wired — see §6 for the rules integration and the new scroll-specific rule actions.

**The router already has a Scrolling passthrough to remove.**
- `src/core/resolve/screenmoderouter.{h,cpp}` — `ScreenModeRouter::engineFor(screenId)` currently
  returns `nullptr` for `Scrolling` and `partitionByMode()` drops scrolling screens into a
  `passthrough` bucket. **Wire the real `ScrollEngine` here** (add it to the switch, remove the
  passthrough fallback for `Scrolling`). `modeFor()` already resolves `Scrolling` from the layout
  assignment cascade — leave that.

**Engine construction — extend the factory + EngineSet.**
- `src/daemon/controllers/enginefactory.{h,cpp}` — `struct EngineSet { crossSurfaceResolver,
  autotile, snap, router }` and `EngineSet createEngines(...)`. Add
  `std::unique_ptr<PhosphorScrollEngine::ScrollEngine> scroll;` to `EngineSet`, construct it in
  `createEngines()` (declaration-order note: the shared `crossSurfaceResolver` must outlive the
  engines), and pass it to the `ScreenModeRouter` ctor.
- `src/daemon/daemon/init_engines.cpp` — `Daemon::initEnginesAndWiring()` moves the engines into
  members and wires signals/adaptors. Mirror the autotile/snap wiring for scroll: hold
  `m_scrollEngine`, connect `settingsPersistRequested`, wire it into the `WindowTrackingService`
  (`enginewiring.cpp`), and give the drag adaptor access if needed.

**Per-context state — reuse the existing machinery.** Both existing engines key state by a
`PlacementStateKey{screenId, desktop, activity}` via `PhosphorEngine::PerScreenStates<T>` fed by a
`ScreenContextTracker` (the daemon pushes current desktop / per-output desktop / activity). Key
`ScrollScreenState` the same way — one strip per (screen, desktop, activity). Study
`libs/phosphor-snap-engine` (`SnapEngine`, `SnapState`, `stateForWindow`) as the closest template
for the ownership/reverse-map pattern.

**Window identity.** Window IDs are **composite `QString` `"appId|instanceId"`** built/parsed by
`PhosphorIdentity::WindowId` (`buildCompositeId`, `extractAppId`, `extractInstanceId`,
`normalizeAppId`) — **not `QUuid`, not a raw handle.** The effect derives the live id from an
`EffectWindow*`; always key tracking by the live id.

**Screen / work-area geometry.** `PhosphorScreens::ScreenManager`
(`libs/phosphor-screens/include/PhosphorScreens/Manager.h`): `screenGeometry(screenId)` (full),
`screenAvailableGeometry(screenId)` (panel/strut-reserved — **use this for the strip work area**).
The authoritative available rect is pushed from the effect via `setCompositorAvailableGeometry(...)`
(effect queries `KWin::clientArea(MaximizeArea)`); gate layout on `isPanelGeometryReady()` /
`panelGeometryReady()`. Gaps/margins are **not** on ScreenManager — they live on
`PhosphorZones::Layout` (`outerGap*` + inner gaps). Multi-monitor and monitor-subdivision
("virtual screens") are first-class here; virtual desktops/activities are engine-state dimensions,
not ScreenManager concepts.

**Getting geometry onto the screen (the effect actuators — reuse, don't reinvent).**
- Daemon → effect over D-Bus: emit `applyGeometryRequested(windowId, x,y,w,h, zoneId, screenId,
  sizeOnly)` (single) or `applyGeometriesBatch(PhosphorProtocol::WindowGeometryList, action)`
  (batch). `WindowGeometryEntry` = `{windowId, x, y, w, h (absolute px), screenId}`. The `action`
  string selects an animation profile path (e.g. `WindowLayoutSwitch`).
- Effect handles them in `kwin-effect/plasmazoneseffect/daemon_apply.cpp`
  (`slotApplyGeometryRequested`, `slotApplyGeometriesBatch`), which call the central mover:
  `PlasmaZonesEffect::applyWindowGeometry(EffectWindow*, const QRect& geometry, bool
  allowDuringDrag, bool skipAnimation, const QString& profilePath)`
  (`kwin-effect/plasmazoneseffect/drag_snap.cpp`). It `moveResize`s immediately then visually
  morphs old→new via `m_windowAnimator`, handles in-flight retarget
  (`RetargetPolicy::PreserveVelocity`), no-op skipping, and deferral during user move/resize.
- Compositor abstraction: `KWinCompositorBridge`
  (`kwin-effect/compositor/compositorbridge.h`) implementing
  `PhosphorCompositor::ICompositorBridge` (`moveResize`, `applyWindowGeometry`, `activateWindow`,
  `raiseWindow`, `frameGeometry`); `WindowHandle` is `EffectWindow*`.
- Animation primitives: `PhosphorAnimation::AnimatedValue<QRectF>`
  (`libs/phosphor-animation/include/PhosphorAnimation/AnimatedValue.h`) — `start`, `advance`,
  `value` (interpolated), `retarget(newTo, policy)`; and the compositor wrapper
  `PlasmaZones::WindowAnimator` (`kwin-effect/compositor/windowanimator.{h,cpp}`), which already
  computes a **pure translation** (plus optional scale) in `applyTransform`
  (`data += desiredPos - actualPos`). Horizontal scroll = an `AnimatedValue<QRectF>` whose `to()`
  differs in x; `retarget` it on focus change. **This translate math already exists** — reuse it.

### 3. The genuinely hard part — off-screen columns & the effect (verify in Phase 0)

A KWin *effect* (unlike Karousel, which is a script) can reach the internal window API and do this
well, but there are real constraints. Resolve these **first**, in a throwaway effect build, before
building the strip:

1. **Paint-transform culling (highest risk).** Determine whether KWin calls the effect's
   `paintWindow()` for a window whose real geometry is (a) partially off-screen and (b) fully
   off-screen. If fully-off-screen windows are culled, a paint-transform cannot pull an
   off-screen window *into* view — so during a scroll animation you must first commit real
   on-screen geometry (via move, see below) for **every** window that is visible at **any** frame
   of the animation (incoming and outgoing included), let paint-transforms only *interpolate*
   between committed positions, and re-park genuinely off-screen windows after the animation
   settles. Windows that stay fully off-screen the whole animation get no transform.
2. **Synchronous `move()`.** Confirm the effect can call `effectWindow->window()->move(pos)`,
   which takes the `MoveResizeMode::Move` branch and applies geometry **synchronously** with no
   client round-trip (cancels queued configures). Scrolling is pure translation (no resize), so
   the most frequent operation should use this reliable path rather than the resize-bundled
   `moveResize`. Column-width changes still resize (async configure) but are infrequent. This is
   also what structurally avoids the "stuck off-screen window" folklore: never set real geometry
   to an extreme off-screen value; park just outside the nearest output.
3. **Input does not follow paint transforms.** `hitTest()` uses real geometry only. A
   paint-transform is safe only for *fully* off-screen (un-clickable) windows — never a partially
   visible one.
4. **Self-write vs external-write.** Because `move()` is synchronous, `frameGeometryChanged` fires
   *inside* the `move()` call, so a re-entrancy flag (`m_applyingOwnMove = true; move(); false;`)
   deterministically identifies engine-driven moves. Engine-driven *resizes* are async → keep a
   small per-window "pending expected geometry" set and match with a **size tolerance** (a client
   may ack a constrained size); reconcile the column to the actual size. Use
   `isInteractiveMove()`/gravity/anchor to separate user drags.
5. **Focus.** For engine-driven focus use the effect's ability to force activation
   (`force=true`, bypassing focus-stealing-prevention) — but only for user-initiated actions
   (focus-follows-scroll, "focus column N"), never background re-tiling. For brand-new windows use
   a single event-driven activation on the window-shown signal, not a retry loop.

**Off-screen strategy decision (make it explicitly and document it):** either (a) give off-screen
columns real geometry parked just off-canvas (Karousel's approach, robust, simple) with the effect
translating only the near-viewport ±1 column during animation, or (b) keep off-screen columns at a
persistent painted offset. Given constraint 1/3, the safe default is: **compute the whole strip
(so any column can be parked/animated), but only ever set real on-screen `move()` geometry for the
visible range ±1 column; park the rest just off the nearest output.** Add a synchronous pure-move
apply path in `daemon_apply.cpp` alongside `applyWindowGeometry` for the scroll (translation) case.

**Open questions to confirm with the maintainer at kickoff** (don't block Phase 0 on them, but
raise them): XWayland windows may differ on configure round-trips — confirm the model holds or
scope them out; under **focus-follows-mouse**, KWin re-focuses whatever is under the cursor after
a scroll — decide warp-cursor vs suppress-policy-during-action vs document-as-unsupported.

### 4. New engine library

Create `libs/phosphor-scroll-engine/` (mirror `libs/phosphor-tile-engine/`'s CMake shape),
**LGPL-2.1-or-later** (reusable lib; SPDX headers on every file: `// SPDX-FileCopyrightText: 2026
fuddlesworth` + `// SPDX-License-Identifier: LGPL-2.1-or-later`; its own `tests/` follow the lib =
LGPL, per CLAUDE.md). Contents:

- **Pure-logic strip model** (no KWin, no Qt-GUI — fully unit-testable in isolation), parameterized
  over the window type where practical:
  ```
  ScrollScreenState { QVector<Column> columns; int activeColumnIdx; qreal viewOffset; /* relative to active column */ }
  Column            { QVector<Tile> tiles; int activeTileIdx; ColumnWidth width; int presetWidthIdx; ColumnDisplay display; }
  Tile              { QString windowId; WindowHeight height; }
  ColumnWidth   = Proportion(qreal) | Fixed(int px) | Preset(int idx)
  WindowHeight  = Auto{weight} | Fixed(int px) | Preset(int idx)
  ColumnDisplay = Normal | Tabbed   // Tabbed: only activeTileIdx laid out at full column height; others hidden + tab strip
  ```
  Plus relayout (strip → per-column absolute px rects against a given work area + gaps; a `Tabbed`
  column lays out only its active tile at full column height and reports the rest as hidden),
  view-offset
  computation (`fit` = minimum scroll, `centered`, per `center-focused-column`), and all the
  structural operations (insert/remove column, consume/expel, move, focus, width/height mutation).
  Keep files ≤1000 lines; split by concern (`state`, `relayout`, `navigation`, `sizing`).
- **`ScrollEngine : PhosphorEngine::PlacementEngineBase`** — implements the required
  `IPlacementEngine` virtuals by driving the strip model, keyed via `PerScreenStates` +
  `ScreenContextTracker`. Implement `capturePlacement`/`restorePlacement`,
  `managedWindowOrder`/`setInitialWindowOrder`, `engineId()`, and `saveState`/`loadState`
  (persist strip order across daemon restart — reuse the existing last-order persistence pattern
  the autotile engine uses).

**IPlacementEngine extension policy.** niri verbs with no interface equivalent
(`consume`/`expel`, `set-column-width`, scroll-the-viewport) should live on `ScrollEngine`'s
**concrete type**, reached from the daemon via a mode check / `dynamic_cast`, **not** forced onto
the shared interface — unless an operation genuinely generalizes. Only add a new pure-virtual to
`IPlacementEngine` if it is broadly meaningful; a new virtual ripples no-op overrides into
`SnapEngine` and `AutotileEngine`, so assess that blast radius before committing to it and prefer
the concrete-type route.

**Do NOT model scrolling as a `TilingAlgorithm`.** That contract
(`libs/phosphor-tiles`, `calculateZones(TilingParams) -> QVector<QRect>`) is a **stateless
full-reflow** with nowhere to hold per-column widths, per-tile heights, the view offset, or focused
indices, and it reflows on window count change — directly violating the §0 invariant. The scroll
strip is stateful; it must be an engine.

### 5. Settings (follow the store-backed idiom exactly)

Scalars have **no member variable and no per-setting load/save** — the value lives in `m_store`
and the schema drives serialization; `reset()` deletes whole groups. Use group `Tiling.Scrolling`
(add `P_CONFIG_GROUP(tilingScrollingGroup, "Tiling.Scrolling")` in `src/config/configkeys.h`;
`Tiling` is already a managed group, so no `managedGroupNames()` edit is needed as long as you nest
under it). Per scalar setting, the edits are:

1. `src/config/configdefaults.h` — default accessor (+ `...Min()`/`...Max()` constexpr for enums).
2. `src/config/configkeys.h` — `P_CONFIG_GROUP`/`P_CONFIG_KEY` accessors (never inline
   `QStringLiteral` for group/key strings).
3. `src/core/interfaces/isettings.h` — `xxxChanged()` NOTIFY signal under `Q_SIGNALS:`.
4. `src/config/settings.h` — `Q_PROPERTY(... READ ... WRITE ... NOTIFY ...)` + getter/setter decls
   (enums exposed to QML as an `int` adapter, e.g. `centerFocusedColumnInt`). No member.
5. Implementation: bools/ints/strings/doubles/colors use the `P_STORE_*` macros in a
   `src/config/settings/*.cpp` partial (e.g. a new `scrolling.cpp`); **enums are hand-written** in
   the `settings/uienums.cpp` style (typed getter/setter + Int adapter with range clamp) — copy the
   existing `setAutotileInsertPosition` / `setAutotileOverflowBehavior` enum pattern.
6. `src/config/settingsschema.cpp` — register each key in the appropriate `append*Schema` (use
   `clampInt(min,max)` + `intChoices({{int(Enum::X),"x"_L1},...})` for enums so labels are stable).
7. `src/dbus/settingsadaptor/settingsadaptor_registry.cpp` — one `REGISTER_BOOL_SETTING` /
   `REGISTER_INT_SETTING` / etc. line per setting that should be D-Bus-visible.

Settings to add (map straight from §1): `centerFocusedColumn` (enum never|always|on-overflow),
`alwaysCenterSingleColumn` (bool), `defaultColumnWidth` (proportion/fixed/client — model as an enum
+ value, or a small struct serialized as two keys), `presetColumnWidths` (list — store as a
delimited string or indexed keys; mirror however the repo already stores list-valued settings),
`presetWindowHeights` (list), `defaultColumnDisplay` (enum normal|tabbed — the mode new columns
open in), scrolling `gaps` (reuse `Tiling.Gaps` if semantics match, else add), struts if needed,
and an animation on/off + duration knob (reuse the animation-profile mechanism if one already
covers layout-switch motion).

**Schema migration:** adding brand-new keys with defaults needs **no** migration (schema defaults
fill absent keys). Only bump `ConfigSchemaVersion` (currently 5, in
`src/config/configmigration.h`) and add a `v5→v6` step (own TU, stamps literal `6`) if you rename
or relocate existing keys — do not for pure additions. No ad-hoc per-key fallback reads (CLAUDE.md).

### 6. Rules integration

PlasmaZones has a rules engine (`libs/phosphor-rules/`) where each rule is a `MatchExpression`
(the WHEN side, over `Field`/`Operator`/value predicates) plus a list of `RuleAction{type, params}`
(the THEN side, dispatched through the process-wide `ActionRegistry`). Scrolling mode must integrate
with it at three levels. **Follow the additive invariant from `docs/rule-expansion-plan.md`: every
consumer reads `rule-slot ?? config-getter` — config stays the authoritative global default and
rules layer additive overrides on top. No config group is retired and no `RuleSet::SchemaVersion`
bump.** (That plan's own action list is already fully shipped; scrolling is a new additive tier
layered the same way.)

**(a) Assigning a screen/context to Scrolling — already works, do not rebuild.** Mode assignment
is persisted *as rules*. `ContextRuleBridge::makeAssignmentRule`/`makeAssignmentActions` emit a
`SetEngineMode` action carrying `modeToWireString(mode)`, and `DisableEngine` carries the same
token for per-mode disable lists. The `"scrolling"` token round-trips end to end today:
`engineModeOptions()` in `libs/phosphor-rules/src/ruleaction_builtins_p.h` already returns
`{"snapping","autotile","scrolling"}`, and `entryFromRuleMatchActions()` in
`libs/phosphor-zones/src/layoutregistry_rulehelpers.cpp` already decodes `"scrolling"` →
`AssignmentEntry::Scrolling`. So once the engine exists and `ScreenModeRouter::engineFor()` returns
it (§2), a rule `SetEngineMode{mode:"scrolling"}` assigns a context to scrolling and
`DisableEngine{mode:"scrolling"}` disables it with **no rules-layer code change**. Verify this
round-trip in a test; do not add parallel plumbing.

**(b) Window rule actions the scroll engine reuses as-is.** These existing Window-domain actions
already do the right thing for a scrolled window and need only be honored by the engine, not
re-added: `float` (opt a window out of the strip — reuse the existing float support per §1),
`setWindowLayer` (`above`/`normal`/`below` keep-above/below), `exclude` (unmanaged), the
appearance family (`setOpacity`, `setBorder*`, `setHideTitleBar`, `overrideDecorationChain`, tint
actions), `excludeAnimations`, and `routeToScreen`/`routeToDesktop` (open on a given output/desktop
= open on that strip). `snapToZone` is snapping-specific and does not apply. Corresponding **match
fields** already exist too (`IsFloating`, `IsTiled`, `KeepAbove`, `WindowType`, `AppId`, etc.), so
"float Slack", "keep Picture-in-Picture above", "exclude the panel" all work against scrolling with
no new vocabulary.

**(c) New scroll-specific rule actions to add (clone the autotile param family).** The autotile
Context-param actions (`setInsertPosition`, `setOverflowBehavior`, `setSplitRatio`, `setMaxWindows`,
`setAlgorithmParam` in `src/ruleaction_builtins_engine.cpp`) are the exact precedent. Add a parallel
scrolling family so per-context and per-app scroll behavior is rule-authorable:
- **Context-domain** (scoped to a screen/desktop/activity, resolved windowless): `setDefaultColumnWidth`
  (proportion/fixed — kind `percent`/`number`), `setCenterFocusedColumn` (enum never|always|on-overflow),
  `setDefaultColumnDisplay` (enum normal|tabbed), and scrolling gap overrides (reuse the existing
  `setInnerGap`/`setOuterGap*` Context actions if the strip uses the same gap model — prefer reuse).
- **Window-domain** (per-app open behavior): `openColumnWidth` (this app opens at width X — kind
  `percent`/`number`), `openTabbed` (bool — this app's window opens in a tabbed column), and
  optionally `openConsumed` vs `openNewColumn` (enum with a token namespace mirroring
  `InsertPositionToken`) to control whether a matched window joins the focused column or starts a
  new one.

For each new action: (1) register `ActionType`/`ActionParam`/`ActionSlot` in
`libs/phosphor-rules/include/PhosphorRules/RuleAction.h`; (2) add a `registerAction(ActionDescriptor{...})`
in `ruleaction_builtins_engine.cpp` (Context) or `_appearance.cpp` (Window props) with its
`ParamSchema` kind, validator, `ActionDomain`, category, and `userAuthorable`; (3) add the consumer
resolver on the phosphor-zones/daemon side as `slot ?? config` — mirror
`LayoutRegistry::resolveContextTilingParams`/`resolveContextGaps` with a new
`resolveContextScrollingParams` + a `ContextScrollingParams` struct on `AssignmentEntry.h`; (4) the
engine (or `windowOpened` path) reads the resolved value. **Reusing an existing `ParamSchema` kind
(`percent`/`number`/`enum`/`bool`) means zero QML change** — only a translated label + default
payload in `src/settings/rules/ruleauthoring.cpp`. A genuinely new kind would need branches in all
three render sites (`ActionRow.qml`, `ActionListView.qml`, `ruleauthoring.cpp`); avoid that by
reusing existing kinds.

**(d) One gotcha — the `Field::Mode` MATCH vocabulary is separate.** The engine-mode *action*
vocabulary already includes `"scrolling"`, but the `Field::Mode` *match* field (used by
`WHEN Mode Equals ...` context gates) currently tokenizes only `"snapping"/"tiling"`. If you want
mode-scoped rules like `WHEN Mode Equals "scrolling" → setInnerGap 8`, add a `"scrolling"` token to
the `Field::Mode` match vocabulary in `MatchTypes.h` (distinct from the action vocabulary — the
in-code note there warns not to unify them). Not required for (a)/(b)/(c); only for mode-gated
context rules.

### 7. Shortcuts

Register through `src/daemon/controllers/shortcutmanager.{h,cpp}` (never KGlobalAccel directly; it
routes via `PhosphorShortcuts::IBackend`). Per action, five edits (the maintainer's own recipe is
in a comment near the top of `shortcutmanager.cpp`):

1. `Q_SIGNAL` in `shortcutmanager.h` (directional actions carry `NavigationDirection`; presets can
   carry an `int`; simple toggles carry nothing).
2. Default accessor in `configdefaults.h` (`xxxShortcut()`).
3. Getter/setter in `src/config/settings/shortcuts.cpp` (`P_STORE_GET`+`P_STORE_SET_STRING`), group
   **`Shortcuts.Tiling`** (`shortcutsTilingGroup`) since these are tiling-family ops.
4. A row in the static `kStaticEntries[]` table in `shortcutmanager.cpp`:
   `{kIdXxx, &ConfigDefaults::xxxShortcut, &Settings::xxxShortcut,
   QT_TRANSLATE_NOOP("plasmazones","Label"), [](ShortcutManager* sm){ Q_EMIT sm->xxxRequested(); }}`
   (fire lambda must be capture-less).
5. Cheatsheet metadata in the `CatalogMeta`/`catalogMetaForId` table with **mode string
   `"autotile"`** (scrolling-column ops belong to the tiling family the cheatsheet already filters;
   if a dedicated `"scrolling"` cheatsheet bucket is wanted, add it consistently in the QML filter
   too).

Actions to register cover the full §1 vocabulary: focus/move column-left/right and window-up/down,
column-first/last, `consume`/`expel` (and the combined consume-or-expel-left/right), `center-column`,
`toggle-column-tabbed-display` (plain no-arg toggle), the width operations (cycle-preset
forward/back, set-width ±%, maximize-column, expand-to-available-width) and the window-height
operations (cycle-preset, set-height ±%, reset-height), plus move-column-to-monitor and
move-column-to-desktop.

Then **wire each signal to a `Daemon` slot** (mirror the autotile connections in
`src/daemon/daemon/autotile_init.cpp`; generic navigation lives in `start.cpp`). Each slot resolves
the active engine via `ScreenModeRouter::engineFor(screenId)`, `dynamic_cast`s to `ScrollEngine`
for scroll-only verbs, and calls it.

**Default keybindings — pick KDE-safe defaults (do not collide with stock Plasma).** niri's
defaults use `Mod`+arrows/HJKL which clash with KDE's desktop/window switching. Prefer a
`Meta+Alt`-anchored scheme for scrolling (as Karousel does for its scroll actions) or leave the
directional focus/move on the existing PlasmaZones navigation shortcuts if the user already has
them bound. Present the proposed default table to the maintainer for approval before finalizing;
ship every action **unbound or on a clearly-free chord**, never silently stealing a stock binding.

### 8. D-Bus surface

Adaptors are **hand-written** `QDBusAbstractAdaptor` subclasses in `src/dbus/<name>adaptor/`, each
with a matching hand-maintained `dbus/org.plasmazones.*.xml` introspection file (kept in sync
manually; installed to `KDE_INSTALL_DBUSINTERFACEDIR`). For columnar operations exposed over D-Bus,
extend `src/dbus/autotileadaptor/` (interface `org.plasmazones.Autotile`) with new `public
Q_SLOTS`/`Q_SIGNALS` and mirror them in `dbus/org.plasmazones.Autotile.xml`, or add a dedicated
`org.plasmazones.Scrolling` adaptor if the surface is large enough to warrant its own interface —
choose based on cohesion. Scalar tunables are already covered by the settings registry (§5.7).

### 9. Settings UI (QML)

Pages live in `src/settings/qml/pages/<feature>/`. Add a scrolling sub-page under the existing
`tiling` virtual parent (itself under `placement`). Two required edits plus optional controller:

1. List every new `.qml` in `qt_add_qml_module(plasmazones-settings ... QML_FILES ...)` in
   `src/settings/CMakeLists.txt` (missing files → runtime "not a type").
2. Register the page in `src/settings/controller/settingscontroller_pageregistration.cpp`
   (`buildApplicationController`) via `regPage(...)` (has a C++ `PageController`) or `regVirtual(...)`
   (binds a `Settings` property directly). Declare Simple/Advanced visibility inline with
   `PageRegistry::PageVisibility` (`PV::Always` | `PV::AdvancedOnly` | `PV::SimpleOnly`), and pass a
   `counterpartId` if you provide both a simple and advanced variant (copy the
   `tiling-simple`/`tiling-algorithm` pairing).
3. If the page needs a controller, add it as a `SettingsController` member and update the topology
   tables in `settingscontroller_pagetopology.cpp` (`pageGroupChildren`, `pageOwnedConfigKeys`,
   `validPageNames`). Also add a **layout-picker entry** ("Scrollable") so a screen can be assigned
   to Scrolling mode.

QML rules: Kirigami `Units`/`Theme` only (no hardcoded spacing/colors), typed/`required`
properties, bindings over JS assignment, zone/window IDs never indices, `i18n()`/`i18nc()` for
strings. Watch the QQC2 pitfalls the repo has hit (binding severing, Loader visibility latches) —
consult existing snapping/tiling pages as the template.

### 10. Tests

Qt Test + ctest under `tests/unit/<area>/`, registered in `tests/unit/CMakeLists.txt` **above the
`ADD NEW TESTS` marker**. Simple pure-logic tests use `p_add_test(name path.cpp)` (links
Qt6::Test/Core + the target lib only). The **pure strip model is the priority test target** — test
it in isolation in `libs/phosphor-scroll-engine/tests/` (LGPL): open→new-column-resizes-nothing,
close/last-window focus selection, consume/expel, width-preset cycling and `±%`, view-offset for
all three `center-focused-column` modes, edge behavior, minimize/restore slot memory, multi-monitor
strip independence, and tabbed-column behavior (toggle normal↔tabbed, active-tile-only layout with
others reported hidden, tab cycling via focus-window-up/down, tabbed columns still consuming/
expelling/moving/resizing like normal ones). Tests needing D-Bus/Wayland are hand-rolled (the `p_add_test` macro deliberately
omits Qt6::DBus) and every test runs under `dbus-run-session --config-file
tests/unit/test-session-bus.conf` (via `TEST_LAUNCHER`) so the installed daemon can't auto-activate
and hang ctest. Model engine/integration tests on `tests/unit/autotile/**`. **Run the full ctest
suite and verify both the unity and no-unity (`build-nounity`) builds succeed before finishing** —
unity builds mask missing includes; src-rooted includes are the convention.

### 11. Cross-cutting rules (from CLAUDE.md — enforce)

- **Licensing/SPDX** on every new file: app/daemon/effect/settings trees = `GPL-3.0-or-later`;
  the new `libs/phosphor-scroll-engine/**` (incl. its `tests/`) = `LGPL-2.1-or-later`. `#pragma
  once` in headers.
- **Qt6 strings:** `QLatin1String`/`_L1` for JSON keys & comparisons, `QStringLiteral` for
  constants/paths; never raw `"..."` with QString/QJsonObject. `QUuid::toString()` with braces
  everywhere except filesystem paths (WithoutBraces) — though IDs here are composite QStrings, not
  QUuids.
- **User-facing prose** (settings labels, descriptions, layout-picker name, CHANGELOG, whatsnew):
  plain human prose — no em-dashes/clause-splicing semicolons/spaced-hyphens/"Label: payload"
  flourishes/rule-of-three. Does not apply to comments or logs.
- **i18n:** C++ uses `PhosphorI18n::tr()` (string tables via `QT_TRANSLATE_NOOP("plasmazones",...)`
  then `PhosphorI18n::tr(...)`); QML uses `i18n()`/`i18nc()`. Run `cmake --build build --target
  update-ts` after adding C++ strings (QML strings are transcribed by the stub script; expect zero
  direct lupdate hits for QML).
- **Files ≤1000 lines** (1150 hard ceiling); split by concern. Signals past-tense, slots
  action-verb, emit only on actual change. No TODO/"for now" hacks — root-cause fixes only.
- Add a **CHANGELOG.md** entry and a `data/whatsnew.json` highlight (plain prose) when the feature
  lands.

### 12. Suggested order of work

1. **Phase 0 — feasibility (do first, in a throwaway effect build):** verify §3 items 1–2 (paint
   culling, synchronous `move()`), then 3 (input dead-zone) and 4 (self-write). If item 1 shows
   fully-off-screen windows are painted anyway, the animation model simplifies; if culled, adopt
   the commit-before-animate ordering. Confirm the `IPlacementEngine` gap (which niri verbs need a
   home) and the concrete-vs-interface decision per §4.
2. **Phase 1 — strip model + engine skeleton:** `libs/phosphor-scroll-engine` pure model +
   `ScrollEngine` skeleton; wire `EngineSet`/`createEngines`/`ScreenModeRouter`/`initEnginesAndWiring`;
   re-entrancy guard + pending-resize set. Unit-test the model.
3. **Phase 2 — lifecycle:** open/close/focus/resize/minimize/fixed-size-floats/VD+activity
   switch/screen hotplug + aggressive window filtering; apply geometry via `move()` (no animation
   yet — snap into place).
4. **Phase 3 — navigation & command vocabulary:** focus/move/consume/expel/center; interactive
   drag (reuse `kwin-effect/dragtracker.cpp`); view-offset `fit`/`centered`; wire ShortcutManager
   with KDE-safe defaults.
5. **Phase 4 — widths/heights, tabbed columns & smooth scroll:** preset cycling, `set-column-width`,
   full-width, window-height presets; the `Column::display` normal/tabbed toggle with active-only
   layout, non-active-tile suppression, and the tab-strip render (per §1 tabbed-column rules);
   smooth animation via `WindowAnimator`/`AnimatedValue<QRectF>` retarget using the §3 ordering;
   `center-focused-column` modes.
6. **Phase 5 — settings, rules, UI, persistence, docs:** all `Tiling.Scrolling` settings + schema +
   registry; the §6 rules integration (verify `SetEngineMode{mode:"scrolling"}` assignment
   round-trips, then add the scroll-specific Context/Window rule actions cloning the autotile param
   family, each resolved `slot ?? config`); QML page + layout-picker "Scrollable" entry; restart
   persistence; CHANGELOG + whatsnew; `update-ts`; full ctest + both build trees green.

Report progress at the end of each phase and surface the §3 open questions (XWayland,
focus-follows-mouse) and the proposed default-keybinding table for approval.

## PROMPT ENDS HERE
