# Drag-policy comparator: snap-path cross-screen detection gap

**Status:** Known issue, not introduced by PR #330 — predates the PhosphorLayer
migration. Documented here so a future drag-policy refactor picks it up.

**Source:** Minor finding #11 from PR #330 review pass 5, reviewer B.

---

## The bug

`WindowDragAdaptor::updateDragCursor` emits `dragPolicyChanged` when a window
drag crosses a screen boundary that changes which mode (snap / autotile /
bypass) should govern the drag. The comparator at
`src/dbus/windowdragadaptor/drag_protocol.cpp:468-469` is:

```cpp
if (candidate.bypassReason != m_currentDragBypassReason
    || (candidate.bypassReason == DragBypassReason::AutotileScreen
        && candidate.screenId != cursorScreenId)) {
    // ... flip ...
}
```

That handles two transitions:

1. **Bypass-reason changes** — e.g. dragging from a snap screen to an
   autotile screen, or to a screen where PlasmaZones is disabled.
2. **Autotile screen-to-screen** — cursor moves between two autotile
   screens, where each has its own tiling state.

It does **not** detect transitions within the **snap path**. If two snap
screens have different per-screen settings, a cross-screen drag between
them keeps the old mode in force:

| Screen A                     | Screen B                     | Result on A→B drag |
|------------------------------|------------------------------|--------------------|
| `snappingEnabled = true`     | `snappingEnabled = false`    | Overlay stays on, snapping still runs — B expected it off |
| `snappingBehavior = Zones`   | `snappingBehavior = Halves`  | Zone overlay stays rendered even though B wants halves |
| zone-selector corner = TL    | zone-selector corner = BR    | Selector renders on wrong corner until drag ends + restarts |

The user's first clue is usually "the overlay is wrong" or "snapping
didn't happen where I expected" — they release, reattempt, and it works.
Not data-destructive but a visible inconsistency.

---

## Why it wasn't fixed here

The PR #330 changes to `drag_protocol.cpp` are the mechanical swap of
the drag-feedback window from the old `createQmlWindow` helpers to
`PhosphorLayer::SurfaceFactory`. Expanding the comparator touches the
snap engine's per-screen settings resolution, `computeDragPolicy`'s
output struct, and the KWin effect's reaction handler — all outside
the PR's scope.

---

## Fix sketch (for a future PR)

The comparator has two options:

### Option A — Expand `DragPolicy` equality

Give `DragPolicy` a full `operator==` that compares every policy-relevant
field, not just `bypassReason` / `screenId`:

```cpp
struct DragPolicy {
    DragBypassReason bypassReason;
    QString screenId;
    bool snappingEnabled;
    SnappingBehavior behavior;
    // ... other per-screen knobs ...

    bool operator==(const DragPolicy&) const = default;
};
```

Then the comparator becomes a single line:

```cpp
if (candidate != m_currentDragPolicy) {
    m_currentDragPolicy = candidate;
    Q_EMIT dragPolicyChanged(windowId, candidate);
}
```

`computeDragPolicy` already resolves every relevant setting per-screen
— this just caches the result instead of discarding it.

### Option B — Per-screen-id cache with dirty-flag

Track the last-emitted policy per screen; re-emit whenever the current
screen's policy differs from the cached one. Simpler if `DragPolicy`'s
field set changes frequently (no need to maintain `operator==`), but
requires cache invalidation on settings change.

---

## Acceptance test (for whoever fixes it)

Minimum reproduction in `tests/unit/test_drag_policy.cpp` (new case):

1. Two mock screens, both snap-path.
2. Screen A: `snappingEnabled = true`, default behavior.
3. Screen B: `snappingEnabled = false`.
4. Start drag on A; `updateDragCursor` at A's coordinates → no flip.
5. Move cursor to B's coordinates → assert `dragPolicyChangedSpy.count() == 1`.
6. Move back to A → assert `dragPolicyChangedSpy.count() == 2`.

Current code fails step 5 (no flip emitted).

---

## Related files

- `src/dbus/windowdragadaptor/drag_protocol.cpp:455-483` — comparator site
- `src/core/dragpolicy.{h,cpp}` — `DragPolicy` struct + `computeDragPolicy`
- `tests/unit/test_drag_policy.cpp` — existing policy tests
- `kwin-effect/plasmazoneseffect.cpp` — `dragPolicyChanged` signal consumer
