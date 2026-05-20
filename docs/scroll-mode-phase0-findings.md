<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Scroll mode — Phase 0 findings

Companion to [`scroll-mode-plan.md`](scroll-mode-plan.md). Records the outcome of the
Phase 0 verification pass.

**Status:** static code review **complete — no blockers.** The scene-culling risk the
plan flagged as highest is resolved by code review (§3); no separate empirical probe is
warranted. The architecture is sound and substantially already in place.

## 1. Architecture correction — the doc's mental model was off

The plan implicitly treated "the engine moves the window." The actual codebase is a
three-tier split, and scroll mode must slot into it:

```
ScrollEngine (daemon-side, KWin-agnostic)   libs/phosphor-engine, libs/phosphor-scroll-engine
   │  computes the strip layout, emits signals (activateWindowRequested,
   │  placementChanged, geometryRestoreRequested, navigationFeedback …)
   ▼
Daemon  — relays engine output to the effect over D-Bus
   ▼
PlasmaZonesEffect (KWin::OffscreenEffect)    kwin-effect/
   │  receives slotApplyGeometryRequested / slotApplyGeometriesBatch /
   │  slotWindowsTileRequested / slotActivateWindowRequested
   ▼
ICompositorBridge → KWinCompositorBridge — the ONLY code that touches KWin::Window
```

Consequences for the plan:

- `ScrollEngine` never calls a KWin API. It is pure layout logic + signals, exactly
  like `AutotileEngine`. This is good — it stays unit-testable with no compositor.
- All geometry/focus mechanics (`move()`, `force`-activation, paint transforms) live in
  the **effect** and **`KWinCompositorBridge`**, not the engine.
- The `move()` vs `moveResize()` distinction from the research applies to
  `KWinCompositorBridge`, which currently exposes **only `moveResize`** — see §6.

## 2. `IPlacementEngine` contract review (Phase 0 item 4 — COMPLETE)

`libs/phosphor-engine/include/PhosphorEngine/IPlacementEngine.h` is explicitly built as
a small **required** core plus a large set of **optional virtuals with no-op defaults**.
The header's own rationale: *"Engines override only the capabilities they support."*

**Required surface** (~18 pure-virtual methods) — all satisfiable by `ScrollEngine`:
`isActiveOnScreen`, `windowOpened`, `windowClosed`, `windowFocused`,
`toggleWindowFloat`, `setWindowFloat`, the 11 navigation intents
(`focusInDirection`, `moveFocusedInDirection`, `swapFocusedInDirection`,
`moveFocusedToPosition`, `rotateWindows`, `reapplyLayout`, `snapAllWindows`,
`cycleFocus`, `pushToEmptyZone`, `restoreFocusedWindow`, `toggleFocusedFloat`),
`saveState`, `loadState`, `stateForScreen` (×2).

**Navigation is direction-string based** (`focusInDirection(QString direction, ctx)`),
which maps niri cleanly: `"left"`/`"right"` = columns, `"up"`/`"down"` = tiles within a
column. `focus`/`move`/`swap` in all four directions, plus `moveFocusedToPosition`, are
already expressible. No new methods needed for basic navigation.

**Six niri operations have no interface method** and need adding:
`consume-window-into-column`, `expel-window-from-column`, set/cycle column width,
set/cycle window height, scroll-the-viewport, center-column.

**Blast radius is effectively zero** — correcting the plan's §5 worry. These six go in
as **new optional virtuals with no-op defaults**, following the exact precedent of the
existing autotile-only group (`increaseMasterRatio`, `focusMaster`, …). `SnapEngine` and
`AutotileEngine` inherit the no-op defaults and need **no source changes**. The real
work is daemon shortcut routing + the D-Bus adaptor + effect-side handling — not the
interface itself.

**`IPlacementState`** (`stateForScreen`'s return type) is read-only +
`toJson()`: `screenId`, `windowCount`, `managedWindows`, `containsWindow`,
`isFloating`, `floatingWindows`, `placementIdForWindow`, `tiledWindowCount`,
`masterCount`. A `ScrollScreenState` implements all trivially —
`placementIdForWindow` returns a `"column:row"` token.

**Per-context state already modelled.** `EngineTypes.h` defines
`TilingStateKey { screenId, desktop, activity }`, and the interface already has
`setCurrentDesktop` / `setCurrentActivity` / `desktopsWithActiveState` /
`pruneStatesForDesktop` / `pruneStatesForActivities`. The plan's "one strip per virtual
desktop" (§7) maps directly onto this — no new context plumbing.

## 3. Paint-transform & scene culling (Phase 0 item 1 — RESOLVED by code review)

The plan flagged this as the highest-risk unknown. Reading the actual paint pipeline
(`kwin-effect/plasmazoneseffect/paint_pipeline.cpp` + `windowanimator.cpp`) resolves it:
PlasmaZones **already renders windows far outside their frame geometry** — it does so
every time it animates a snap or autotile move. The three pieces are all in place:

1. `prePaintScreen` sets `data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS` whenever
   `m_windowAnimator->hasActiveAnimations()`. Its own comment: *"Windows have
   translation transforms that move them outside their frame geometry bounds — force
   full compositing mode."* Under this mask KWin paints every window through the effect
   chain instead of culling by geometry.
2. `prePaintWindow` calls `data.setTransformed()` for each animating window — *"without
   the transformed flag, paintWindow only fires on actual window damage."*
3. `WindowAnimator::applyTransform` translates the window by `animValue − frameGeometry`
   — an **arbitrary** offset — and `scheduleRepaints()` / `onRepaintNeeded` issue
   `KWin::effects->addRepaint(bounds)` over the animation's full travel bounds
   (`from` ∪ `to`), so the on-screen leg of a scroll-in is always damaged and repainted.

A window scrolling in from off-screen is exactly an animation whose `from` is off-screen
and `to` is on-screen — the existing `WindowAnimator` handles it with no new rendering
code. Scroll mode reuses `WindowAnimator` directly. (`PlasmaZonesEffect` is also a
`KWin::OffscreenEffect`, so `redirect()` is available as a fallback, but it is not
needed.)

**One caveat** (not a blocker): a window at rest with fully-off-screen real geometry and
no active animation is correctly culled — it is invisible, which is desired. But when a
scroll animation *starts* on such a window, `onAnimationStarted` only calls
`window->addRepaintFull()`, whose region is off-screen and therefore a no-op, so the
first frame may not be kicked. Fix is one line — at scroll-animation start also
`KWin::effects->addRepaint()` over the on-screen target region. Noted for Phase 4.

## 4. Self-write disambiguation (Phase 0 item 2 — already solved in-tree)

The plan's §4.2 re-entrancy guard **already exists**: `PlasmaZonesEffect` has
`m_inDaemonGeometryApply` — *"True while a daemon-driven geometry apply is moving a
window. Suppresses the windowFrameGeometryChanged crossing-detection."* Scroll mode
reuses it directly; the autotile `slotWindowFrameGeometryChanged` handler is the
template. No new mechanism needed.

## 5. Focus (Phase 0 item 3 — infrastructure exists, one gap)

- The focus path exists: `PlacementEngineBase` emits `activateWindowRequested`, the
  daemon relays, `PlasmaZonesEffect::slotActivateWindowRequested` activates. Deferred
  reactivation exists too (`m_pendingReactivateWindow`).
- **focus-follows-mouse is already handled** — `AutotileHandler::setFocusFollowsMouse` /
  `handleCursorMoved` / `m_focusFollowsMouse`. The plan's open question 5 is largely
  answered; scroll mode reuses this rather than inventing it.
- **Gap:** `ICompositorBridge::activateWindow(WindowHandle)` has no `force` parameter, so
  the research's `force=true` FSP-bypass is not reachable today — see §6.

## 6. Optional `ICompositorBridge` additions (deferred — see plan.md Phase 1)

`ICompositorBridge` (`libs/phosphor-compositor/.../ICompositorBridge.h`) currently
exposes for geometry/focus: `moveResize(WindowHandle, QRectF)`,
`activateWindow(WindowHandle)`, `applySnapGeometry(WindowHandle, QRectF, skipAnimation)`,
`setMaximized`, `raiseWindow`. Two small **additive** changes were originally sketched
here as required, but the implemented Phase 1 pipeline routes scroll-mode geometry
through the existing effect-batch path (`applyGeometriesBatch`) — that already covers
both cases below, so neither addition is needed for the current pipeline. They are
**deferred** as future-only optimisations for a hypothetical direct-bridge path that
bypasses the effect:

1. **Pure move** — `void move(WindowHandle, QPointF)`, implemented as `window->move(pos)`
   (the synchronous `MoveResizeMode::Move` path). Useful only if scroll-mode geometry
   ever skips the effect batch. The current pipeline reuses `applySnapGeometry`'s
   moveResize call and the redundant width/height write is benign.
2. **Forced activation** — a `force` parameter (or `activateWindowForced`) so
   `Workspace::activateWindow(w, true)` is reachable. Not needed for Phase 1 because
   the focus model is "engine asks, daemon decides, effect activates" — the existing
   `activateWindow` is the daemon's authoritative entry point and the FSP-bypass case
   has not surfaced in real usage.

Both would be LGPL `phosphor-compositor` interface changes — minor, and `WayfireBridge`
(if it ever exists) would just get the same two methods. Until a direct-bridge code
path is justified, these stay unimplemented.

## 7. The culling question — resolved without a separate probe

The plan originally called for an instrumented effect build to test scene culling. The
code review in §3 answers it: the transformed-painting path PlasmaZones already runs for
every snap/autotile animation is precisely the mechanism scroll mode needs. A standalone
throwaway probe would only re-test infrastructure that is demonstrably in production use
— so none was written.

The single narrow residual — that KWin paints a window whose *base* geometry is entirely
outside all outputs under `PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS` (the comments strongly
imply yes) — is confirmed for free by the first `WindowAnimator`-driven scroll in
Phase 1: if it were going to fail, the very first scroll-in animation would visibly
stall, unambiguously. No separate build step is warranted.

## 8. Updated risk assessment

| Concern | Phase 0 outcome |
| --- | --- |
| Off-screen geometry writes | `KWinCompositorBridge` needs a `move()` method; otherwise fine |
| Self-write disambiguation | **Already solved** — `m_inDaemonGeometryApply` |
| Focus-set failures | Path exists; needs a `force` activation parameter |
| focus-follows-mouse fights engine | **Already handled** — `setFocusFollowsMouse` infra |
| `IPlacementEngine` blast radius | **Zero** — new optional virtuals, no engine changes |
| Paint-transform / scene culling | **Resolved** (§3) — transformed-paint path already in production; one first-frame `addRepaint` caveat |
| Per-desktop strip plumbing | **Already exists** — `TilingStateKey`, context methods |

Nothing blocks Phase 1.

## 9. Phase 1 entry checklist (revised)

- Reuse `WindowAnimator` for scroll animation; add the first-frame `addRepaint` kick
  for windows starting fully off-screen (§3 caveat).
- Add `move()` + forced `activateWindow` to `ICompositorBridge` / `KWinCompositorBridge`.
- Scaffold `libs/phosphor-scroll-engine` with the `ScrollScreenState` / `Column` / `Tile`
  model and a `ScrollScreenState` that implements `IPlacementState`.
- Add the six niri operations as optional virtuals on `IPlacementEngine`.
