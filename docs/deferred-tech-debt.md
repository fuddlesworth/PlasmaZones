# Deferred Technical Debt

Tracked issues identified during code review (2026-02-26) that are safe to defer
but should not be forgotten. None have immediate runtime risk.

---

## M2: buildWindowMap Multi-Instance Bug

**Severity:** Medium | **Type:** Functional bug (batch operations only)
**File:** `kwin-effect/plasmazoneseffect.cpp` — `buildWindowMap()`

`buildWindowMap` uses `extractStableId()` as the `QHash` key. When two windows of
the same app class are open (e.g. two Dolphin windows), they share the same
`stableId` and `QHash::insert` silently overwrites the first. The last window in
stacking order wins; the other is invisible to batch operations.

**Affected operations:** rotate, resnap-all, snap-all-windows
**Unaffected:** individual snap, drag-and-drop, navigation (these use full windowId)

**Fix requires:**
- Change `QHash` to `QMultiHash<QString, KWin::EffectWindow*>`, OR
- Use full window IDs as keys (breaks daemon-side stableId matching)
- Update all callers to handle multiple windows per stableId

**Trigger to prioritize:** User reports that rotate/resnap skips one window when
multiple instances of the same app are open.

---

## M3: Removed 100ms Delay in slotWindowAdded

**Severity:** Low | **Type:** Potential startup timing race
**File:** `kwin-effect/plasmazoneseffect.cpp` — `slotWindowAdded()`

The old 4-step async snap chain had a 100ms `QTimer::singleShot` before the first
D-Bus call. This was removed when switching to the single `callResolveWindowRestore`
call. The D-Bus round-trip (~1-5ms) provides some natural delay, and
`applySnapGeometry` retries up to 20x at 100ms intervals if the window is still in
user-move state.

**Remaining concern:** `getWindowScreenName(window)` is called synchronously at
`windowAdded` time. If KWin hasn't finalized screen assignment yet, the screen name
could be wrong. The retry mechanism in `applySnapGeometry` doesn't help because it
retries with the same (possibly wrong) geometry.

**Fix if needed:** Add a 25-50ms delay before `callResolveWindowRestore`, or add
screen-name validation on the daemon side.

**Trigger to prioritize:** User reports windows snapping to the wrong monitor at
session startup or when opening apps on slow/complex multi-monitor setups.

---

## M4: Per-Screen QML Override Pattern Duplicated 3x

**Severity:** Low | **Type:** DRY violation (no runtime impact)
**Files:**
- `kcm/ui/tabs/AutotilingTab.qml`
- `kcm/ui/tabs/ZoneSelectorSection.qml`
- `kcm/ui/tabs/ZonesTab.qml`

All three implement an identical per-screen override pattern:
- `selectedScreenName` property
- `isPerScreen` / `hasOverrides` computed properties
- `perScreenOverrides` cache object
- `reloadPerScreenOverrides()` function
- `settingValue(key, globalValue)` reader
- `writeSetting(key, value, globalSetter)` writer

Only the C++ method names differ (`getPerScreenAutotileSettings` vs
`getPerScreenZoneSelectorSettings` vs `getPerScreenSnappingSettings`).

**Fix:** Extract a reusable `PerScreenOverrideMixin` QML component or JS module
that takes the C++ method names as parameters.

**Trigger to prioritize:** Next time any of these three files needs the override
pattern modified — update all three or extract first.

---

## D1: Snap Result Handling Boilerplate in Old D-Bus Methods

**Severity:** Low | **Type:** DRY violation (mitigated)
**File:** `src/dbus/windowtrackingadaptor.cpp`

The four individual snap methods (`snapToLastZone`, `snapToAppRule`,
`snapToEmptyZone`, `restoreToPersistedZone`) share ~20 lines of identical
output-assignment + markAsAutoSnapped + clearFloatingStateForSnap +
assignWindowToZone boilerplate. The new `resolveWindowRestore` method already
factors this into an `applySnap` lambda.

These old methods are now secondary — the KWin effect uses `resolveWindowRestore`
as the primary path. The individual methods remain for direct D-Bus testing or
potential external callers.

**Fix:** Extract a private helper:
```cpp
void WindowTrackingAdaptor::applySnapResultToOutputs(
    const SnapResult& result, const QString& windowId,
    int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap);
```
Each method would reduce to ~5 lines.

**Trigger to prioritize:** If the boilerplate needs updating again (as happened
when clearFloatingStateForSnap and multi-zone support were added).
