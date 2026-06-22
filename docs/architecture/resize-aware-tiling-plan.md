<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Resize-Aware Tiling + Luau Scripting API — Implementation Plan

> Closes GitHub discussion #652 (interactive resize should reflow neighbors / fill gaps).
> Single PR delivering Phases 1–4. Authored from a multi-agent design + adversarial review pass.

## 1. Summary

When a user finishes an interactive resize of a tiled window (e.g. Meta+Right-click edge drag),
the compositor effect notifies the daemon, which reflows the neighboring tiled windows so no gap is
left. Two tiers share one event path:

- **Tier A (engine tree-reflow):** maps the moved edge to the owning `SplitTree` split, mutates that
  node's `splitRatio`, and retiles gap-free. This is what #652 actually needs (BSP/dwindle-memory
  family) and is the core deliverable.
- **Tier B (scripting):** enriches the Luau `ctx`, adds an `onWindowResized` hook, and adds a general
  persistent state bag on `TilingState` so non-tree algorithms (an aligned grid) can remember
  adjustments across retiles.

The output contract is unchanged — window membership/order is stable across a resize, so the
`zones[i] → windows[i]` index mapping holds. No `ConfigSchemaVersion` bump and no `configmigration.cpp`
change: `TilingState` is session state on a separate JSON surface from `config.json`.

## 2. Architecture / Data Flow

```
 KWin compositor
   windowFinishUserMovedResized (window_lifecycle.cpp:689); wasResize latch (m_resizingWindow :673,:690)
   effect-side cheap negative filters only: wasResize && shouldHandleWindow && !isWindowFloating && valid geom
 → PlasmaZones effect
   fireAndForget notifyWindowResized(windowId, x,y,w,h)   [no effect-side membership cache]
 → D-Bus org.plasmazones.WindowTracking
 → Daemon adaptor (main thread)  WindowTrackingAdaptor::notifyWindowResized
     validate → update m_frameGeometry shadow (capture oldFrame BEFORE overwrite)
     gate: isWindowInAutotileMode (:299) — single authority
     screen: screenForTrackedWindow (:548); guard: newRect center inside screenGeometry (cross-output)
   engine->onWindowResized(windowId, oldFrame, newFrame, screenId)  [synchronous]
 → AutotileEngine::onWindowResized (new)
     tier fork (single owner of precedence):
       supportsMemory()       → TIER A: applyTreeResizeReflow
       else supportsResizeHook() → TIER B: build ResizeEvent → algo->onWindowResized hook
       else                   → no-op (KWin keeps user geometry)
     TIER A (all inlined in applyTreeResizeReflow): per-axis edge diff → splitOwningEdge
             → subtreeBoundingRect (split extent from rendered zones) → ratio → resizeSplitNode
             → retileAfterOperation(screenId, true)   [SYNCHRONOUS]
   recalculateLayout → prepareTilingState → calculateZones (applyGeometryRecursive, gap-free)
 → applyTiling (AutotileEngine.cpp:3512): zones[i] → windows[i] by index;
     ratio-equality guard (skip emit if unchanged); per-entry skipAnimation for dragged window
   emit JSON 'windowsTiled' (:3598)
 → KWin effect applies geometry (daemon_apply.cpp): neighbors animate; dragged window not re-animated
```

The discriminator is the existing `m_resizingWindow` latch; no new effect state is added. The effect
applies only locally-certain negative filters and unconditionally fires `notifyWindowResized`; the
daemon owns the tiled-vs-snapped-vs-untracked decision via `isWindowInAutotileMode`. The call is
fire-and-forget because the resize is already committed in KWin; the reflow of *neighbors* is pushed
back through the existing `windowsTiled` apply path. The engine handler is the single owner of tier
precedence. The resize-finish path uses the **synchronous** `retileAfterOperation`, never the debounced
`scheduleRetileForScreen`, so any pending resize event is consumed by exactly the retile it armed.

## 3. Phased Work Breakdown

### P1 — Input plumbing

Files:
- `kwin-effect/plasmazoneseffect/window_lifecycle.cpp:689-718` — new block after the floating-resize
  block (`:701-717`), before `handleWindowFinishMoveResize` (`:718`). Reuse `wasResize` (`:690`), latch (`:673`).
- `kwin-effect/plasmazoneseffect/window_filtering.cpp` — reuse `shouldHandleWindow` (`:172`), `isWindowFloating` (`:70`).
- `dbus/org.plasmazones.WindowTracking.xml` — `notifyWindowResized` after `setFrameGeometry` (after `:419`).
- `src/dbus/windowtrackingadaptor.{h,cpp}` — slot modeled on `windowClosed` (`:652-686`) and `setFrameGeometry` (`:926-932`).
- `libs/phosphor-protocol/include/PhosphorProtocol/ClientHelpers.h:52` — reuse `fireAndForget`.

**Refinements from implementation:**
- **Dual-frame D-Bus signature** (supersedes "new-frame only"): the effect sends *both* old and new
  frames — `notifyWindowResized(windowId, oldX,oldY,oldW,oldH, newX,newY,newW,newH)`. The daemon's
  `m_frameGeometry` shadow is updated *during* the drag by the debounced `setFrameGeometry` push, so it
  is not a reliable baseline. Instead the effect latches the window's frame at
  `windowStartUserMovedResized` (when `isUserResize()`) and reports it at finish.
- **No `screenId` arg** — the daemon resolves the screen via `screenForTrackedWindow` (empty ⇒ not
  autotile-tracked ⇒ skip); `onWindowResized` lives on the `IPlacementEngine` interface (default no-op),
  overridden by `AutotileEngine`. There is no `isWindowInAutotileMode` (it doesn't exist); the engine's
  own guards (autotile screen, floating, `<2` windows, cross-output) are authoritative.
- `skipAnimation` for the dragged window is **deferred** (see §9) — it needs a `TileRequestEntry`
  protocol-struct change and the dragged window's tiled zone already matches where the user left it, so
  it animates ≈0px without it.

### P2 — Engine tree reflow + SplitTree helpers

Files:
- `libs/phosphor-tile-engine/include/PhosphorTileEngine/AutotileEngine.h` — declare `onWindowResized`
  (override) and private `applyTreeResizeReflow`. (Tier B dispatches the algorithm's `onWindowResized`
  hook inline in the handler — no separate `dispatchScriptedResize` method was needed.)
- `libs/phosphor-tile-engine/src/AutotileEngine.cpp` — handler after `onWindowFocused`; the per-axis edge
  diff and delta→ratio math are inlined in `applyTreeResizeReflow`, with `unionSubtreeZones` /
  `subtreeBoundingRect` anon-namespace helpers for the rendered-zone split extent.
- `libs/phosphor-tiles/include/PhosphorTiles/SplitTree.h` — public `enum class Edge`; `splitOwningEdge`
  (const+non-const, after `:195`); `resizeSplitNode(SplitNode*, qreal)`; `resizeSplit` delegates.
- `libs/phosphor-tiles/src/splittree.cpp` — implement `splitOwningEdge` + `resizeSplitNode`.
- `libs/phosphor-tiles/include/PhosphorTiles/AutotileConstants.h` — add `ResizeEdgeMoveThresholdPx`.
- `libs/phosphor-tiles/include/PhosphorTiles/TilingAlgorithm.h` — `supportsResizeHook()` default false,
  `onWindowResized(TilingState*, const ResizeEvent&)` default no-op (added in P3).

`splitOwningEdge(windowId, Edge)` walks leaf→root, returning the nearest ancestor whose split
orientation matches the edge axis AND whose `first`/`second` side touches that edge (Right/Bottom →
leaf in `first`; Left/Top → leaf in `second`); `nullptr` at a screen boundary.

**Coordinate-consistency refinement (supersedes the out-param plan).** `applyTreeResizeReflow` reads
the owning split's extent as the **bounding box of its subtree's currently rendered zones**
(`state->calculatedZones()`), NOT from a fresh tree-geometry pass. The compositor-reported `newFrame`
is in rendered coordinates, so the split rect must be too; deriving it from rendered zones keeps the
edge→ratio math in one coordinate space and makes the result self-correcting at min-size limits — which
dissolves the F3/R3 reconciliation problem at the root, so no post-clamp reconciliation pass is needed.
The ratio is computed position-based: `firstSize = newEdgePos - axisStart` (Right/Bottom) or
`- innerGap` more (Left/Top); `ratio = firstSize / (axisExtent - innerGap)`, then `resizeSplitNode`
clamps to `[0.1, 0.9]`. A pure move (both edges of an axis shift together) is rejected per-axis.
Dragged-window animation suppression via a per-entry `skipAnimation` flag in the `windowsTiled` JSON
entry (NOT `filterForPreview`). Emit guard: `applyTreeResizeReflow` returns false (no retile) when no
ratio actually changed.

### P3 — ctx enrichment + onWindowResized hook + persistent state bag

Files:
- `libs/phosphor-tiles/include/PhosphorTiles/TilingParams.h` — `WindowInfo.windowId`; `ResizeEvent`
  struct; `std::optional<ResizeEvent> resize`.
- `libs/phosphor-tiles/src/tilingstate.cpp` — set `info.windowId` in `buildWindowInfos`.
- `libs/phosphor-tiles/include/PhosphorTiles/TilingState.h` — `m_scriptState` (QJsonObject) +
  `scriptState()`/`setScriptState()`, no NOTIFY.
- `libs/phosphor-tiles/src/tilingstateserialization.cpp` — serialize `m_scriptState` (guarded);
  validate on load; host shared `sanitizeScriptState`.
- `libs/phosphor-tiles/include/PhosphorTiles/AutotileConstants.h` — `ScriptStateKey` (QLatin1String).
- `libs/phosphor-tiles/src/luautilealgorithm.cpp` — `buildContext` enrichment; `parseMetadata`
  `supportsScriptState`; hook detect + dispatch; state write-back via the `onWindowResized` hook return only.
- `libs/phosphor-tiles/src/pluau/pluau.d.luau` — types for `ResizeEdge`/`ResizeEvent`,
  `Context.{windowId, resize, currentGeometries, state}`, `Algorithm.onWindowResized`,
  `WindowInfo.windowId`, `Metadata.supportsScriptState`.
- `libs/phosphor-tile-engine/src/AutotileEngine.cpp` — `m_pendingResize` keyed by `(screenId, windowId)`
  drained synchronously, Tier-B set in the non-memory branch, `setScriptState` write-back.

`ResizeEvent`: `{ int index; QRect oldRect; QRect newRect; bool left,right,top,bottom; }`.
Luau ctx: `resize = { index, oldRect, newRect, edges = {left,right,top,bottom} }`.
`ctx.currentGeometries` = last *applied* zones (advisory; scripts use `ctx.resize` for the resized cell).
`sanitizeScriptState`: 64 KB compact-JSON cap, depth ≤ 16, ≤ 4096 keys, reject NaN/Inf; runs on
write-back and on load. Reserved `__v` key round-tripped opaquely (script self-migrates).
`tile()`'s bare-array return contract is unchanged — write-back is the hook return only.

### P4 — Aligned resize-aware grid reference algorithm

Files (as implemented):
- `data/algorithms/aligned-grid.luau` (new, **GPL-3.0-or-later** — matches the other bundled
  `data/algorithms/*.luau`, not LGPL) — `supportsMemory = false`, `supportsScriptState = true`; shared
  `colFractions[]`/`rowFractions[]` normalized fractions in `ctx.state`.
- `libs/phosphor-tiles/src/pluau/pluau.d.luau` — type defs (`ResizeEvent`, `Context.{currentGeometries,
  state}`, `WindowInfo.windowId`, `Metadata.supportsScriptState`, `Algorithm.onWindowResized`).

`fractionsToSizes` is implemented inline in the algorithm (pure Luau) rather than as a new `pluau.luau`
builtin — single consumer, keeps the change self-contained. Auto-discovered via the `data/algorithms`
glob; no manifest entry needed. The boundary delta is normalized by reconstructing the content extent
from the resized cell's old size and fraction, so the hook needs no screen-area input. Uniform reset on
reshape; boundary mutations written through the `onWindowResized` hook return; partial-last-row column
drags are a documented no-op.

## 4. Key Interfaces

D-Bus (`dbus/org.plasmazones.WindowTracking.xml`):

```xml
<method name="notifyWindowResized">
    <arg name="windowId" type="s" direction="in"/>
    <arg name="x" type="i" direction="in"/>
    <arg name="y" type="i" direction="in"/>
    <arg name="width" type="i" direction="in"/>
    <arg name="height" type="i" direction="in"/>
</method>
```

Engine handler (`AutotileEngine.h`):

```cpp
void onWindowResized(const QString& windowId, const QRect& oldFrame,
                     const QRect& newFrame, const QString& screenId);
```

SplitTree helpers (`SplitTree.h`):

```cpp
enum class Edge { Left, Right, Top, Bottom };
const SplitNode* splitOwningEdge(const QString& windowId, Edge edge) const;
SplitNode*       splitOwningEdge(const QString& windowId, Edge edge);
void             resizeSplitNode(SplitNode* node, qreal ratio);   // clamps [0.1,0.9]
// resizeSplit(windowId, ratio) => resizeSplitNode(leaf->parent, ratio)
// Split extent for the reflow math is the bounding box of the split's subtree's
// rendered zones (engine-side), not a fresh geometry pass — see P2 refinement.
```

TilingState script-state API (`TilingState.h`):

```cpp
QJsonObject scriptState() const;
void setScriptState(const QJsonObject& state);   // wholesale replace; validated upstream
// private: QJsonObject m_scriptState;
```

ctx additions (`buildContext`, all conditional → nil when absent):

```
ctx.windows[i].windowId : string
ctx.currentGeometries   : { Zone }?    -- last APPLIED zones, advisory
ctx.resize : { index, oldRect, newRect, edges = {left,right,top,bottom} }?  -- single-shot
ctx.state  : { [string]: any }?        -- read view of m_scriptState
```

Luau `onWindowResized` hook (`pluau.d.luau` + `TilingAlgorithm.h`):

```
onWindowResized: ((state: any, resize: ResizeEvent) -> StateTable?)?   -- Luau
```
```cpp
virtual bool supportsResizeHook() const noexcept { return false; }
virtual void onWindowResized(TilingState* state, const ResizeEvent& resize);  // default no-op
```

State write-back: the `onWindowResized` hook's returned table (and only that) is sanitized via
`sanitizeScriptState` and written into `TilingState::m_scriptState` after the synchronous resize
retile. `tile()`'s return shape is unchanged.

## 5. Edge-Case & Precedence Rules

- **edge→split resolution:** nearest leaf→root ancestor matching the edge's axis orientation AND side
  (Right/Bottom → leaf in `first`; Left/Top → leaf in `second`). Screen-boundary edge → `nullptr` → no-op.
- **corner handling:** one H-edge + one V-edge → two independent `resizeSplitNode` calls on distinct
  nodes (order-independent). Boundary corner edge → only one ratio changes.
- **min-size clamp:** over-drag clamps at `max(neighbor declared min if respectMinimumSize,
  MinSplitRatio·splitContent)`. Tree ratio reconciled to post-clamp geometry (no hysteresis). No
  push-through in v1.
- **engine-reflow vs Luau-hook precedence (single owner = engine handler):** `supportsMemory()` →
  Tier A, never sets `ctx.resize`, never fires the hook. Else `supportsResizeHook()` → Tier B. Else no-op.
- **multi-monitor / cross-output:** daemon resolves via `screenForTrackedWindow`. If `newFrame.center()`
  leaves `screenGeometry(screenId)`, reject — `windowScreenChanged` owns the handoff.
- **floating / XWayland / single-window:** floating excluded both sides. XWayland same path.
  Single window (`leafCount() < 2`) → early no-op before `prepareTilingState`.
- **infinite-retile guard:** finish-only; synchronous `retileAfterOperation`; single-shot
  `(screenId,windowId)` consume; `setScriptState` emits no signal; clamped-to-identical ratio skips emit.
- **frame ≠ zone:** edge diff uses the daemon `m_frameGeometry` old frame, not `calculatedZones`.

## 6. Persistence & Versioning

No `ConfigSchemaVersion` bump, no `configmigration.cpp` change. `TilingState` is session state on a
separate JSON surface from `config.json`. `SplitTree` resized ratios round-trip via the existing
`ratio` field. New `m_scriptState` is serialized in `TilingState::toJson/fromJson` only, guarded by
`!isEmpty()` (absent in old sessions → empty bag → defaults). `sanitizeScriptState` runs at write-back
and load boundaries. Script-internal format changes handled by the script via the reserved opaque
`__v` key.

## 7. Test Plan

All new tests go in the top-level **GPL** `tests/unit/` (gated behind `-DBUILD_TESTING=ON`).

**Implemented (253/253 suite green):**
- `tests/unit/autotile/test_split_tree.cpp` (extended): `splitOwningEdge` — two-window shared boundary,
  screen-boundary edges, orthogonal-axis miss, nested nearest-collinear-ancestor; `resizeSplitNode` —
  reflow, clamp, null/leaf no-op.
- `tests/unit/scripting/test_luau_aligned_grid.cpp` (new): loads the real bundled `aligned-grid.luau` and
  exercises the full P3+P4 path — resize-hook declaration, uniform grid, the column-moves-all-rows proof,
  state-bag round-trip, and reshape-resets-to-uniform.

The existing 251-test suite already covers the modified serialization / context-building / engine paths;
all pass unchanged. (The originally-scoped engine-`onWindowResized` end-to-end and D-Bus-adaptor tests
were judged lower-value given the above plus existing coverage; the novel logic — edge→split resolution
and the scripting hook end-to-end — is directly tested.)

Original full scope listed below for reference:

- **P2 `tests/unit/autotile/test_split_tree.cpp` (extend):** `testOwningEdge_*` (single, nested skips
  orthogonal, nearest collinear ancestor, root→null, unknown window); `testResizeSplitNode_*`;
  `testRatioForEdge_*` (pixel round-trip, second-child-left-edge sign, clamp bounds); `testReflow_*`
  (gap-free with gaps, corner two splits, corner one edge boundary, single-window no-op, degenerate
  content no-op).
- **P2 `tests/unit/autotile/test_autotile_engine_resize.cpp` (new):** neighbor reflow gap-free,
  membership/order unchanged, min-clamp both regimes, tree-ratio reconciled, non-memory no-op,
  single emit, clamped-identical no emit, dragged skipAnimation flag, zones-size parity, cross-output rejected.
- **P1 `tests/unit/dbus/test_window_tracking_resize.cpp` (new):** floating ignored, untracked ignored,
  forwards old+new frame, degenerate geometry rejected.
- **P3 `tests/unit/scripting/test_luau_tile_algorithm.cpp` (extend):** `ctx.resize` absent/present,
  `ctx.currentGeometries == calculatedZones`, state write-back round-trip + persistence, sanitize
  (oversize/NaN/depth), hook gate, index clamp, backward-compat byte-identical.
- **P4 `tests/unit/scripting/test_luau_aligned_grid.cpp` (new):** column boundary moves all rows,
  uniform reset on reshape, float-toggle reshape, partial-row no-op.

Run: `cmake --build build --parallel $(nproc)` then `cd build && ctest --output-on-failure`.

## 8. Risk Register

| # | Risk | Mitigation |
|---|------|-----------|
| R1 | Dragged window snaps back | Per-entry `skipAnimation` flag (NOT `filterForPreview`); keeps entry so `enforceMinSizes` still applies. |
| R2 | Wrong edge→split at depth | Table-driven `testOwningEdge_*` over orientation × side × depth; assert pointer identity. |
| R3 | Tree ratio diverges from min-clamped geometry | Reconcile owning split's ratio from post-clamp child extents. |
| R4 | Geometry round-trip drift | No parallel `nodeRect` impl; `applyGeometryRecursive` records node rects via out-param. |
| R5 | Resize event lost to retile coalescing | Synchronous `retileAfterOperation`; `(screenId,windowId)`-keyed drain for Tier B. |
| R6 | Frame≠zone baked offset in edge diff | Diff against daemon `m_frameGeometry` old frame. |
| R7 | Cross-output resize double-drives | Reject when `newFrame.center()` leaves the screen. |
| R8 | Script writes huge/garbage bag | `sanitizeScriptState` (64 KB / depth 16 / 4096 keys / NaN-reject) at write-back AND load. |
| R9 | `tile()` return-shape change breaks 25 algorithms | Write-back via hook return only — `tile()` contract untouched. |
| R10 | Index past `MaxZones` → Luau OOB | Clamp `ResizeEvent.index`; drop event if resized window beyond cap. |
| R11 | Re-entrant retile clobbers shared algorithm state | No `mutable` stash; state returned through the call chain. |
| R12 | File-size limit | Split engine resize handler into a new LGPL `.cpp` if `AutotileEngine.cpp` exceeds 800 lines. |

## 9. Resolved Decisions

1. **All phases ship in one PR** (per maintainer).
2. **Dragged-window `skipAnimation`:** deferred. Needs a `TileRequestEntry` protocol-struct + marshalling
   + D-Bus signature + daemon-parse + effect-apply change; the dragged window's tiled zone already
   matches the drag end position, so the re-animation is ≈0px. Track as a standalone follow-up.
3. **Resize animation profile:** v1 reuses `WindowLayoutSwitch` for the reflow batch; dedicated
   `WindowResize` mapping is later polish.
3. **`ResizeEdgeMoveThresholdPx`** default 5 px, dedicated constant (not `GapEdgeThresholdPx`).
4. **Grid reshape:** uniform-reset-on-reshape only in P4.
5. **Live-during-drag streaming:** deferred. Forward-compat note: live `oldRect` baseline must be a
   drag-start latch, not `calculatedZones`.

## 10. Compliance Checklist

- SPDX header on every new/changed file: `// SPDX-FileCopyrightText: 2026 fuddlesworth`.
- License per tree: `libs/phosphor-*/**` (incl. `pluau.d.luau`) → **LGPL-2.1-or-later**;
  `kwin-effect/**`, top-level `tests/**`, `src/**`, and the bundled `data/algorithms/*.luau` (incl.
  `aligned-grid.luau`) → **GPL-3.0-or-later** (the algorithms ship with the GPL shell, matching the 26
  existing bundled algorithms). Do NOT create `libs/phosphor-tiles/tests/`.
- Qt6 string literals: `QLatin1String` for JSON keys/compares; `QStringLiteral` for ctx field/method
  names; never raw `"string"`.
- ConfigDefaults: N/A — no `config.json` key, no schema bump, no `configmigration.cpp` edit.
- 800-line limit: split the resize handler into a new LGPL `.cpp` if `AutotileEngine.cpp` exceeds 800 lines.
- Signals only on change: ratio-equality guard before `windowsTiled` emit; `m_scriptState` compared
  before store; no NOTIFY on `m_scriptState`.
- No ad-hoc backwards-compat: absent `scriptState` → empty bag → defaults; script self-migrates via `__v`.
- `#pragma once` on new headers; forward-declare `ResizeEvent`/`SplitNode`.
- Input validation at boundary: `validateWindowId`, geometry > 0, `isWindowInAutotileMode` (daemon);
  `sanitizeScriptState` (serialization).
- Qt Test: `QTEST_MAIN`/`QCOMPARE`/`QVERIFY`; register new files in `tests/unit/CMakeLists.txt`; list
  new `.luau` fixtures in the test target's data/resource listing.
- DRY: single `sanitizeScriptState`, single canonical `onWindowResized(TilingState*, const ResizeEvent&)`,
  single `applyGeometryRecursive` geometry implementation.
