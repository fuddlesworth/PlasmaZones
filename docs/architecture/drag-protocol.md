# Drag Protocol — v3 design note

**Status**: implemented on `feat/drag-protocol-refactor` branch for v3.
**Scope**: kwin-effect ↔ daemon drag event routing.
**Fixes**: discussion [#310](https://github.com/fuddlesworth/PlasmaZones/discussions/310) (post-settings-reload dead-drag window, Meta-F float toggle lag/drop).

## Problem

Before the refactor, drag routing decisions (autotile bypass vs snap path vs dead-on-disabled) were made in the kwin-effect using local state caches:

- `m_autotileScreens` — set updated by async `autotileScreensChanged` D-Bus signal from daemon
- `m_dragBypassedForAutotile` — bool flag set at drag start from `m_autotileScreens.contains(startScreenId)`
- `m_cachedZoneSelectorEnabled` — bool shadow of `Snapping.ZoneSelector.Enabled`

Each of these was **eventually consistent** with the daemon's authoritative state. During the ~ms window after a settings reload, the effect's cache could disagree with the daemon. The log forensics on #310 showed **7 consecutive dead drags over ~41 seconds** following a settings reload, with the daemon rejecting each one at the "Snapping disabled in settings" early-return gate — the effect had sent `dragStarted` believing it was a snap-path drag, but the daemon's authoritative state said snap was off. No autotile fallback fired because `m_dragBypassedForAutotile` was set to the wrong value at drag start.

The float-toggle shortcut had a different but related symptom: Meta-F made **4 D-Bus hops across 3 processes** (KWin → daemon → effect → daemon → engine → effect) and stalled under any D-Bus backpressure, producing the "pressed Meta-F, nothing happened, ... seconds later it toggled" user experience.

Both bugs have the same shape: **policy decisions made from local state that should be daemon-owned**.

## Design

The effect becomes a dumb relay. The daemon owns all policy decisions. Two new D-Bus methods (`beginDrag`, `endDrag`), one new fire-and-forget method (`updateDragCursor`), and one new signal (`dragPolicyChanged`) form the new surface. Typed structs `DragPolicy` and `DragOutcome` carry the decisions.

### Wire format

```
[Plugin → Daemon]   beginDrag(windowId, frameX, frameY, frameW, frameH,
                              startScreenId, mouseButtons)
                    → DragPolicy {
                          streamDragMoved,
                          showOverlay,
                          grabKeyboard,
                          captureGeometry,
                          immediateFloatOnStart,
                          screenId,
                          bypassReason
                      }

[Plugin → Daemon]   updateDragCursor(windowId, cursorX, cursorY,
                                     modifiers, mouseButtons)
                    — fire-and-forget, 30Hz throttled by plugin

[Daemon → Plugin]   dragPolicyChanged(windowId, DragPolicy)
                    — emitted when daemon detects cursor crossed a
                      virtual-screen boundary that flips the policy

[Plugin → Daemon]   endDrag(windowId, cursorX, cursorY, modifiers,
                            mouseButtons, cancelled)
                    → DragOutcome {
                          action,  // NoOp / ApplyFloat / ApplySnap /
                                   // RestoreSize / CancelSnap /
                                   // NotifyDragOutUnsnap
                          windowId,
                          targetScreenId,
                          x, y, width, height,
                          zoneId,
                          skipAnimation,
                          requestSnapAssist,
                          emptyZones  // populated only if requestSnapAssist
                      }
```

### `computeDragPolicy` — the decision function

A pure static function on `WindowDragAdaptor` that encapsulates the entire routing decision. Inputs: `ISettings*`, `AutotileEngine*`, windowId, screenId, desktop, activity. No side effects. Precedence (first match wins):

1. **`context_disabled`** — activity / desktop / monitor excluded in display settings
2. **`autotile_screen`** — the autotile engine owns window placement on this screen
3. **`snapping_disabled`** — top-level `Snapping.Enabled = false` on a non-autotile screen
4. **Canonical snap path** — everything else

This ordering means a user with `Snapping.Enabled = false` and autotile active on both monitors (exactly the #310 reporter's config) gets the correct `autotile_screen` policy — autotile wins over snap-disabled because it's listed first.

Pinned by an 8-state truth table in `tests/unit/dbus/test_drag_policy.cpp`.

### Cross-VS flip handling

When `updateDragCursor` is called during a drag, the daemon resolves the cursor screen, re-computes the policy at that screen, and compares against the currently-active policy. If the `bypassReason` changes (or the screen changes while both sides are autotile), the daemon emits `dragPolicyChanged` with the new policy.

The plugin's `slotDragPolicyChanged` handler diffs old vs new and applies the compositor-level transition:

- **snap → autotile**: cancel snap overlay, enter autotile bypass, clear pending snap state. Do NOT call `handleDragToFloat` mid-drag — it schedules an `applySnapGeometry` that would race against the drop-time snap (see the comment in the handler for the original debug story).
- **autotile → snap**: drop bypass flag, call `onWindowClosed` on the old autotile screen to clear tracking state, initialize snap-drag state at the new cursor position, grab keyboard.
- **autotile → different autotile** (same `bypassReason`, different `screenId`): update tracked bypass screen id. Drop-time `endDrag` will apply `ApplyFloat` to the final screen.
- **snap → snap**: no-op.

### `endDrag` — the dispatch

Internally calls the legacy `dragStopped` (still present as a private C++ helper) for the snap path to preserve the intricate overlay/zone/snap-assist logic, and packages the nine out-parameters into a `DragOutcome` struct. For bypass paths, composes the outcome directly (autotile → `ApplyFloat`, disabled → `NoOp`).

The plugin's `callEndDrag` helper sends the D-Bus call and applies the returned outcome verbatim via a switch on `DragOutcome::action`:

- `ApplySnap` / `RestoreSize` — paint via `applySnapGeometry` (same as the legacy path)
- `ApplyFloat` — call `handleDragToFloat` + `setWindowFloatingForScreen` for the drop screen
- `NoOp` / `CancelSnap` / `NotifyDragOutUnsnap` — nothing to paint; daemon handled its own cleanup
- `requestSnapAssist` true — show the window picker via `asyncShow`

Auto-fill on empty-zone drop is still done by `tryAsyncSnapCall` when no other action applied and the daemon supplied a release screen.

## What got deleted

From `kwin-effect/plasmazoneseffect.cpp`:

- `callDragStarted` / `sendDeferredDragStarted` — the "defer dragStarted until activation trigger detected" optimization is obsolete. `beginDrag` is unconditional; the daemon always knows about the drag from the moment it begins.
- `callDragMoved` / `callDragStopped` — replaced by the inline `updateDragCursor` fire-and-forget call in the dragMoved lambda and the `callEndDrag` helper respectively.
- The effect-side cross-VS flip loop (~80 lines in the dragMoved lambda) — moved to daemon-side detection in `updateDragCursor` + reaction handler `slotDragPolicyChanged`.
- The autotile special-case branch in the dragStopped lambda (~60 lines of sub-case dispatch for snap→autotile, cross-VS, same-VS) — collapsed into `ApplyFloat` handling inside `callEndDrag`.

From `dbus/org.plasmazones.WindowDrag.xml`:

- `dragStarted` / `dragMoved` / `dragStopped` methods — no longer part of the D-Bus introspection surface. Still exist as private C++ helpers called by `drag_protocol.cpp` internally.

## Related: float toggle (phase 2)

The Meta-F bug from the same #310 report had a different root cause (not drag policy, just the 4-hop D-Bus chain) but was fixed as part of the same branch.

- Phase 1 added a frame-geometry shadow in `WindowTrackingAdaptor`, populated via `setFrameGeometry` D-Bus pushes from the effect on `windowFrameGeometryChanged` (50ms debounced).
- Phase 2 rewrote `WindowTrackingAdaptor::toggleWindowFloat` to use `m_lastActiveWindowId` + the frame-geometry shadow and dispatch to `toggleFloatForWindow` in-process. The old effect round-trip, the stale `isWindowFloating` local cache read on the effect side, and the 100ms debounce on `Daemon::handleFloat` are all gone.
- Net change: Meta-F → visible toggle latency drops from "seconds under backpressure" to sub-50ms.

## Invariants

1. **Single source of truth**. The daemon's `computeDragPolicy` is the only place that answers "what kind of drag is this". No effect-side cache shadows it.
2. **First-match precedence**. `computeDragPolicy` checks disables in a fixed order (context → autotile → snap-disabled → canonical). The resulting `bypassReason` string is stable across coincidental disables.
3. **Fire-and-forget hot path**. `updateDragCursor` is the only 30Hz call. Daemon replies (signals) flow back async.
4. **Mid-drag transitions are daemon-driven**. The plugin never diffs its own state to decide when to flip modes; it only reacts to `dragPolicyChanged`.
5. **Parent gates are compile-time**. `ISettings::isZoneSelectorActive()` and `isSnapAssistActive()` encode `snappingEnabled() && child` so consumers can't forget the parent check.

## Why not sync `beginDrag`?

Early drafts used a synchronous D-Bus call at drag-start so the plugin would have the policy before dispatching the first mouse move. Rejected because:

- Sync D-Bus on the compositor thread risks deadlock if the daemon is mid-callback into the plugin.
- The ~ms reply window is tolerable: the plugin defaults to a conservative snap-path policy while waiting, and a rare brief overlay flash is much better than a dead drag.
- If the reply arrives mid-drag with a different policy, `slotDragPolicyChanged` retroactively applies the transition via the same handler as cross-VS flips. Same machinery, same correctness.

## Files

- `src/compositor-common/dbus_types.{h,cpp}` — `DragPolicy` / `DragOutcome` types + QDBusArgument streaming
- `src/dbus/windowdragadaptor.h` + `src/dbus/windowdragadaptor/drag_protocol.cpp` — `beginDrag` / `endDrag` / `updateDragCursor` / `computeDragPolicy`
- `dbus/org.plasmazones.WindowDrag.xml` — D-Bus interface
- `kwin-effect/plasmazoneseffect.{h,cpp}` — plugin-side port (`callEndDrag`, `slotDragPolicyChanged`)
- `tests/unit/dbus/test_drag_policy.cpp` — 8-state truth table
- `src/core/interfaces.h` — `isZoneSelectorActive()` / `isSnapAssistActive()` composite accessors

## Commits

`feat/drag-protocol-refactor` on top of `v3`:

- `4330f22c` — phase 1: frame-geometry shadow
- `b04b1b7c` — phase 2: daemon-local float toggle
- `992b9e52` — phase 3a: DragPolicy types + beginDrag + truth table test
- `d97ddfaf` — phase 3b: endDrag + DragOutcome
- `355a55c9` — phase 3c: effect calls beginDrag
- `46d92844` — phase 3d: updateDragCursor + dragPolicyChanged signal
- `cd916b50` — phase 3e: effect port (cross-VS flip deletion, callEndDrag)
- `0dac0613` — phase 3f: delete legacy dragStarted/dragMoved/dragStopped from D-Bus surface
- `e8a80896` — phase 4: composite settings accessors
