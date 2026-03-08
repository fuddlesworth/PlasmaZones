# ADR: SnapEngine Migration — Deferred Items

**Status**: Complete
**Date**: 2026-03-08
**Context**: SnapEngine extraction from WindowTrackingAdaptor

## Context

The SnapEngine was extracted to mirror AutotileEngine's architecture, implementing the shared IWindowEngine interface. This ADR tracks deferred items that require larger architectural changes beyond the initial extraction.

## Completed (This Session)

| # | Issue | Fix |
|---|-------|-----|
| 2 | `setWindowFloat(false)` limbo state | Keep window floating when no pre-float zone exists instead of clearing state |
| 3 | Empty `m_lastActiveScreenName` in `setWindowFloat` | Documented; falls through gracefully now |
| 4 | `windowClosed` double-cleanup | Made SnapEngine::windowClosed a no-op; WTS cleanup stays in adaptor |
| 7 | Empty `screenName` in navigationFeedback | Pass `m_lastActiveScreenName` or method-arg screen in all feedback signals |
| 8 | `setEngines()` double-connect risk | Added disconnect-before-connect guard |
| 9 | Dead `getValidatedActiveLayout()` | Removed from header and persistence.cpp |
| 10 | `rectToJson` duplication | Extracted to `GeometryUtils::rectToJson()`; both classes delegate |
| 13 | Include path inconsistency | Standardized float.cpp to `core/...` project-root-relative style |
| 14 | `QLatin1String` vs `QStringLiteral` | Unified to `QStringLiteral` in `serializeRotationEntries` |
| 15 | Redundant `zoneIds.isEmpty()` check | Removed dead condition |
| 16 | Unused includes in `windowtrackingadaptor.cpp` | Removed 10 unused includes |
| 17 | Unused `<QSet>` in adaptor header | Removed |
| 18 | Snap navigation bypasses SnapEngine | Daemon handlers now call SnapEngine directly (focus, swap, move, snap, rotate, cycle, resnap, snapAll, push) — mirrors autotile routing pattern |
| 19 | Missing snap-specific engine methods | Added `moveInDirection()` and `pushToEmptyZone()` to SnapEngine |
| 20 | Undocumented signal asymmetry | Enhanced `setEngines()` comments explaining why autotile float goes through daemon lambda vs snap float going direct |
| 21 | `resnapIfManualMode` routed through WTA | Changed to route through `m_snapEngine->resnapToNewLayout()` |
| 22 | Toggle-autotile resnap routed through WTA | Changed to route through `m_snapEngine->resnapCurrentAssignments()` |
| 23 | SnapEngine signal relay in ad-hoc `setEngines()` | Created `SnapAdaptor` — constructor-based signal relay matching AutotileAdaptor's pattern; `setEngines()` reduced to cross-references only |
| 24 | `SnapAdaptor::clearEngine()` no-op | Stored `m_adaptor` pointer; disconnect targets `m_engine→m_adaptor` (the actual receiver) |
| 25 | `AutotileAdaptor::clearEngine()` missing disconnect | Added `disconnect(m_engine, nullptr, this, nullptr)` before nulling; moved to .cpp for full type access |
| 26 | WTA nav methods emit raw signals instead of delegating | `moveWindowToAdjacentZone`, `focusAdjacentZone`, `swapWindowWithAdjacentZone`, `snapToZoneByNumber`, `pushToEmptyZone` now delegate to SnapEngine |
| 27 | `setEngines(nullptr, nullptr)` stale cross-references | Always calls `setZoneDetectionAdaptor`/`setAutotileEngine` (passes nullptr when clearing) |
| 28 | `windowOpened` auto-snap chain in WTA | Moved 4-level fallback chain to `SnapEngine::resolveWindowRestore()`; WTA delegates; new `snapengine/lifecycle.cpp` |
| 29 | `saveState`/`loadState` stubs | SnapEngine delegates via `std::function` callbacks; daemon wires WTA's KConfig layer |
| 30 | Unfloat geometry duplication | Extracted `WindowTrackingService::resolveUnfloatGeometry()` with `UnfloatResult` struct; both SnapEngine and WTA delegate |
| 31 | Default params on virtual `windowOpened` | Removed defaults from pure virtual + overrides; added non-virtual 2-arg convenience overload on `IWindowEngine` with `using` declarations |
| 32 | Dangling pointer risk in SnapEngine | Changed `m_autotileEngine` and `m_zoneDetectionAdaptor` to `QPointer`; setters moved to .cpp |

## Previously Deferred Items — All Resolved

### ~~1. Migrate `windowOpened` Auto-Snap Chain to SnapEngine~~ ✓ COMPLETED (#28)

The 4-level auto-snap fallback chain now lives in `SnapEngine::resolveWindowRestore()` (`snapengine/lifecycle.cpp`). WTA's `resolveWindowRestore()` delegates to SnapEngine and unpacks the `SnapResult` to D-Bus out-params. The individual D-Bus methods (`snapToLastZone`, etc.) remain on WTA as standalone entry points.

### ~~2. Migrate `saveState`/`loadState` to SnapEngine~~ ✓ COMPLETED (#29)

SnapEngine delegates to WTA's KConfig layer via `std::function` callbacks set by the daemon during engine wiring. This keeps KConfig out of WTS/SnapEngine while fulfilling the IWindowEngine persistence contract.

### 3. Unified Lifecycle Dispatch Layer — DEFERRED (architectural constraint)

Navigation is fully unified (daemon → engine for both modes). Lifecycle dispatch (`windowOpened/Closed/Focused` routing via `isActiveOnScreen`) was evaluated for unification but deferred due to architectural constraints:

1. **Pre-autotile geometry**: The effect must save window geometry *before* any D-Bus call (daemon has no geometry access). Requires `isAutotileScreen` branching in the effect regardless of D-Bus unification.
2. **shouldAutoSnapWindow**: Effect-side window-type and PID filtering gates snap D-Bus calls. Cannot move to daemon.
3. **Effect-local cleanup**: AutotileHandler cleans up 8+ local tracking sets on windowClosed regardless of daemon routing.

Unifying the D-Bus call alone would add routing complexity to the daemon while the effect retains identical `if (autotileScreen)` branching for its local work. Net savings: ~5 lines in the effect. Not worth the added indirection.

### ~~4. Route Daemon Navigation Through SnapEngine~~ ✓ COMPLETED (previous session)

### ~~5. Extract Shared `unfloatToZone` Geometry Resolution~~ ✓ COMPLETED (#30)

Extracted to `WindowTrackingService::resolveUnfloatGeometry()` returning `UnfloatResult`. Both `SnapEngine::unfloatToZone()` and `WTA::calculateUnfloatRestore()` delegate to it.

### ~~6. Structural Duplication: `clearFloatingStateForSnap` / `applyGeometryForFloat`~~ PARTIALLY RESOLVED

**`clearFloatingStateForSnap`** — ✓ RESOLVED. State logic extracted to `WindowTrackingService::clearFloatingForSnap()` (returns bool). Both SnapEngine and WTA delegate to it, then emit their own `windowFloatingChanged` signal. Business logic is single-source.

**`applyGeometryForFloat`** — Accepted as structural duplication. SnapEngine's copy serves snap float paths (engine signals → SnapAdaptor → D-Bus), WTA's copy serves autotile float paths (daemon lambdas → WTA signals → D-Bus). Different callers, different signal sources — cannot be unified without merging the architectural layers.

### ~~7. Default Parameters on `IWindowEngine::windowOpened`~~ ✓ COMPLETED (#31)

Removed defaults from the pure virtual and all overrides. Added a non-virtual 2-arg convenience overload on `IWindowEngine` with `using` declarations in derived classes to avoid hiding.

### ~~8. Dangling Pointer Risk~~ ✓ COMPLETED (#32)

Changed to `QPointer<AutotileEngine>` and `QPointer<ZoneDetectionAdaptor>`. Setters moved out-of-line to SnapEngine.cpp for full type access. Combined with the `setEngines(nullptr, nullptr)` fix (#27), dangling pointer risk is eliminated.

## Decision

All deferred items resolved. #3 (lifecycle dispatch) is permanently deferred — the effect must branch on screen type for local tracking regardless of D-Bus unification, making the change net-negative. #6 (duplication) is partially resolved: `clearFloatingStateForSnap` extracted to WTS; `applyGeometryForFloat` accepted as structural duplication across architectural layers.

## Hive Review Fixes (Post-Completion)

| # | Severity | Fix |
|---|----------|-----|
| 33 | `resolveWindowRestore` misleading "pure decision logic" comment | Corrected to document side effects (consumePendingAssignment, navigationFeedback emit) |
| 34 | `windowOpened` double-assignment risk | Added `isWindowSnapped` guard before running fallback chain |
| 35 | `setWindowFloat` loses caller screenName | Now resolves from WTS `screenAssignments()` first, falls back to `m_lastActiveScreenName` |
| 36 | SnapAdaptor stale "8 signal connections" comment | Updated to "10 signal connections" |
