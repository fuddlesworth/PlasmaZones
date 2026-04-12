# Effect ↔ Daemon Resilience & Performance Plan

Status: proposal
Target branch: `v3`
Scope: harden the KWin effect ↔ daemon path, eliminate the single-threaded-daemon stalls, and tighten the D-Bus protocol surface.

## Problem statement

Recent `v3` commits have been patching symptoms of two structural issues:

1. **Single-threaded daemon.** Layout recomputation, zone geometry, `EmptyZoneList` construction, and every D-Bus handler run on the Qt GUI main thread. A slow handler stalls the compositor waiting on a synchronous reply.
2. **Brittle effect↔daemon IPC.** Missing timeouts, orphaned state on reconnect, untyped `a{sv}` surfaces, and no protocol version gate have produced crashes (`EmptyZoneList` KWin crash, `60d39273`) and subtle races (drag-start restore, `c329981c`).

This document plans the fix across four phases, ordered so that zero-risk wins land first and the protocol bumps are bundled into a single renegotiation.

## Guiding constraints

- D-Bus stays as the transport. Reconnect, `QDBusServiceWatcher`, typed struct marshalling, and `daemonReady` gating already work — throwing that out costs months for marginal wins.
- No temporary workarounds. Per project rules, every fix resolves the root cause.
- Each phase must leave `main` buildable and the effect↔daemon pair protocol-compatible within the phase.
- Phase C is the only protocol bump. Bundle all schema changes there.

## Phase overview

| Phase | Theme | Protocol bump | Blast radius |
|---|---|---|---|
| A | Zero-risk pain relief | No | Local, 5 independent PRs |
| B | Structural: compute worker thread | No | One focused PR behind runtime flag |
| C | Protocol hardening | Yes (single bump) | Coordinated effect + daemon PR |
| D | Deferred: D-Bus handler thread | No | Only if profiling demands |

---

## Phase A — Zero-risk pain relief

Five independent, small PRs. Each is local, no protocol change, each addresses a crash or stall class seen in recent commits.

### A1. Timeout + cancellation on effect-side `dragStopped`

- **File:** `kwin-effect/.../plasmazoneseffect.cpp` (around line 2849, the `QDBusPendingCallWatcher` for `dragStopped`).
- **Change:** Wrap the watcher with a `QTimer::singleShot(500ms, …)` that cancels and logs on expiry; on cancel, leave the window at its release position and clear effect-side drag state.
- **Test:** Unit test with a mocked daemon that delays the reply; verify watcher deletes, no leaked `QPointer<KWin::EffectWindow>`.
- **Done when:** No indefinite compositor wait on a stalled daemon. Window stays put, warning logged.

### A2. Dirty-flag cache for `recalculateZoneGeometries`

- **File:** `src/dbus/windowdragadaptor/drag.cpp:194` (`prepareHandlerContext`).
- **Change:** Add `m_zoneGeometryDirty` on `LayoutManager`, set by `onLayoutChanged` and screen-geometry signals. Skip `recalculateZoneGeometries` inside `dragMoved` when the flag is clean.
- **Test:** Unit test on `LayoutManager` verifying geometry cache invalidation triggers (layout swap, screen resize, activity change).
- **Done when:** A drag across an unchanged layout produces zero full zone recomputes after the first tick.

### A3. Reset daemon drag state on effect reconnect

- **Files:** `src/dbus/windowdragadaptor/*.cpp`, `src/daemon/daemon.cpp` (wherever `slotDaemonReady` or compositor registration is handled).
- **Change:** When the effect re-registers via `CompositorBridge`, explicitly clear `m_draggedWindowId`, `m_currentZoneId`, `m_paintedZoneIds`, and any in-flight drag caches. Add a `resetDragState()` method on `WindowDragAdaptor` and call it from the bridge-register path.
- **Test:** Integration test: start drag, simulate effect unregister/re-register, verify daemon state is clean and the next `dragStarted` is accepted.
- **Done when:** No orphaned drag state survives an effect reload.

### A4. Async startup in `VirtualDesktopManager::refreshFromKWin`

- **File:** `src/core/virtualdesktopmanager.cpp:186`.
- **Change:** Replace the `QDBus::Block` 1 000 ms wait with `asyncCall` + `QDBusPendingCallWatcher`. Initialize `m_currentDesktop` to a documented sentinel (`1`) so downstream code has a valid value before the reply lands; apply real value in the callback and emit `currentDesktopChanged` only on change.
- **Test:** Daemon startup should not block when KWin is slow/absent.
- **Done when:** No synchronous D-Bus calls on the daemon startup path.

### A5. Audit `OverlayService::m_shaderTimerMutex`

- **File:** `src/daemon/overlayservice.{h,cpp}` (mutex at `overlayservice.h:411`).
- **Change:** Identify which thread writes `m_frameCount`/`m_lastFrameTime`. If the RHI render thread touches these, marshal the `QTimer` trigger back to the main thread via `QMetaObject::invokeMethod(this, …, Qt::QueuedConnection)`. Remove the mutex around the `QTimer*` (QTimer is not thread-safe — the mutex gives false safety).
- **Test:** ThreadSanitizer run on a shader-heavy overlay session.
- **Done when:** Clear single-thread ownership of the timer, no TSAN warnings.

### Phase A exit criteria

- Five PRs merged into `v3`.
- All recent crash-class regressions (EmptyZoneList, drag-start restore, daemon-restart-mid-drag) covered by tests.
- No protocol changes. Effect and daemon remain interchangeable with any existing v3 build.

---

## Phase B — Structural: layout/compute worker thread

Single focused PR. This is where the single-threaded brittleness actually gets fixed.

### Design

**New unit:** `src/core/layoutmanager/layoutworker.{h,cpp}` — a `QObject` moved to a dedicated `QThread`. Owns an internal command queue.

**Command types:**

```cpp
struct RecalculateZoneGeometriesCommand { QRect screenGeom; /* immutable snapshot */ };
struct BuildEmptyZoneListCommand { QUuid layoutId; QRect windowGeom; };
struct ResolveLayoutForScreenCommand { QString screenId; };
```

Each command carries an **immutable snapshot** of the layout state it needs (copy, not pointer). Results come back on the main thread via `QueuedConnection` signals (`zoneGeometriesReady`, `emptyZoneListReady`). The command queue is coalescing — a newer `RecalculateZoneGeometries` supersedes an in-flight one for the same screen, using a generation counter (same pattern as `m_autotileStaggerGeneration`).

### Migration steps

1. **Extract pure functions.** Move `recalculateZoneGeometries` and `buildEmptyZoneList` into free functions or static methods that operate purely on an input snapshot. No `LayoutManager` member access.
2. **Introduce snapshot type.** `LayoutSnapshot` — a POD copy of layout data sufficient for geometry computation. Keep cheap via `QSharedDataPointer` if needed.
3. **Wire the worker.** Spawn `LayoutWorker` in `Daemon::start()`, connect command signals (main → worker, `QueuedConnection`) and result signals (worker → main, `QueuedConnection`).
4. **Feature flag.** `ConfigDefaults::layoutWorkerThreadEnabled()` — default `false` for the first merge, flip to `true` after soak testing. Main-thread fallback path stays functional until flag removed.
5. **Rewire `dragMoved` and `dragStopped`.** Callers emit the command; the reply signal triggers the rest of the handler via continuation (lambda + `QPointer` guard).
6. **Remove main-thread fallback.** Once the flag has been `true` on `v3` for a week with no regressions, delete the fallback and the flag in a follow-up PR.

### Explicit non-goals

- `OverlayService` stays on the main thread. QML/RHI state is not thread-safe; threading it would reintroduce the exact race class Phase A5 fixes.
- `WindowTrackingService` stays on the main thread. Its hot path is short and tied to D-Bus dispatch.
- No lock-based sharing. Message passing only.

### Test plan

- Unit tests on `LayoutWorker` with synthetic command sequences including coalescing cases.
- Integration: drag a window across a 20-zone layout and measure `dragStopped` latency distribution before/after. Target p99 < 16 ms (one compositor frame).
- ThreadSanitizer run across the daemon test suite.

### Phase B exit criteria

- `recalculateZoneGeometries` and `buildEmptyZoneList` no longer execute on the main thread when the flag is on.
- p99 `dragStopped` latency < 16 ms on a large-layout drag.
- No regressions in Phase A fixes.
- Feature flag removal PR landed.

---

## Phase C — Protocol hardening (single bundled bump)

One coordinated PR touching effect + daemon. Single version bump via `CompositorBridge.apiVersion`.

### C1. Split `dragStopped` into fast sync + async `snapAssistReady`

- **Goal:** Remove `EmptyZoneList` construction from the blocking reply path. The compositor only needs the snap target rect to complete the drop animation; snap-assist hints can arrive after.
- **New shape:**
  - `dragStopped(...) → DropResult` — returns only `snapTargetGeometry`, `snappedZoneId`, `success`. Fast, no list walk.
  - New signal `snapAssistReady(EmptyZoneList)` — emitted from the daemon's layout worker when the empty-zone list is ready.
- **Effect side:** Completes the drop immediately on `DropResult`; applies snap-assist hints asynchronously when the signal arrives or discards if a new drag started.
- **Protocol:** `apiVersion` bump. Older effect talking to newer daemon gets a clear error from the `registerBridge` handshake.

### C2. Replace `getPreTileGeometriesJson` with a typed struct list

- **File:** `src/dbus/windowtrackingadaptor/signals.cpp:393` and effect-side consumer.
- **Change:** Define `WindowPreTileGeometryEntry { QString windowId; QRectF geometry; QString screenId; int desktop; }`, register via `qDBusRegisterMetaType`, update XML, drop JSON marshalling on both sides.

### C3. Enforce protocol version gate

- **File:** `src/dbus/compositorbridgeadaptor.cpp` and effect registration path.
- **Change:** Add `minApiVersion` constant on both sides. `registerBridge` refuses if the peer is older than the minimum; logs a clear error, the effect disables itself with a user-visible notification. `apiVersion` now actually enforced, not merely returned.

### C4. Type the remaining `a{sv}` surfaces

- `Shader` interface (`org.plasmazones.Shader.xml:18-36`): define `ShaderInfo`, `ShaderParamList` typed structs.
- `LayoutManager` assignment maps (`layoutadaptor/assignment.cpp:357,370`): define `ScreenAssignmentEntry` typed list.
- Rationale: any future adaptor written with an unqualified slot parameter on a `QVariantMap` surface reintroduces the EmptyZoneList-class crash. Closing these surfaces eliminates the hazard category.

### C5. Compile-time guard for unqualified struct parameters

- Add a `static_assert` / SFINAE helper in `compositor-common/dbus_types.h` that refuses fully-qualified namespace struct types lacking `qDBusRegisterMetaType` registration. Enforced at adaptor-header inclusion time so a new typed adaptor cannot compile without registering its types.

### Phase C exit criteria

- Single `apiVersion` bump across all the above.
- Zero `a{sv}` on hot paths; `Shader` typed.
- Protocol version mismatch produces a clean, visible error — not a silent marshalling crash.
- Effect + daemon ship together; older pair combinations are explicitly rejected in `registerBridge`.

---

## Phase D — Deferred: D-Bus handler thread

**Only pursued if Phase B profiling shows main-thread D-Bus dispatch still bottlenecks the GUI loop.**

### Trigger conditions

- p99 drag-handler latency > 8 ms after Phase B, **or**
- Profiling shows `QDBusConnection::send` / adaptor dispatch as a significant portion of main-thread time during drags.

### Approach (if triggered)

- Move `WindowDragAdaptor`, `WindowTrackingAdaptor`, `AutotileAdaptor` onto a dedicated D-Bus thread via `QDBusConnection::connectToBus` per-thread.
- Audit every slot for main-thread-only Qt interactions (`QTimer`, QML, `QWindow`, `OverlayService`) and marshal those via `QueuedConnection`.
- Feature flag, same pattern as Phase B.

### Rejection conditions

- If p99 is already < 8 ms after Phase B, **do not land Phase D.** The complexity-to-payoff ratio is poor.

---

## Explicitly rejected alternatives

- **Full IPC transport rewrite to unix socket / shared memory.** D-Bus reconnect, typed marshalling, and watcher infrastructure are working. Custom transport would reinvent D-Bus badly. Rejected.
- **Full actor model / one thread per service.** Would thread `OverlayService` (QML/RHI) — guaranteed race hazard. Rejected.
- **Lock-based `LayoutManager` sharing.** Trades stalls for deadlocks. Message passing only.
- **Ad-hoc per-key migration for protocol changes.** Per CLAUDE.md rules — schema version bump, single migration path.

---

## Tracking

| Phase | PRs | Status | Owner |
|---|---|---|---|
| A1 | effect dragStopped timeout | planned | — |
| A2 | zone geometry dirty flag | planned | — |
| A3 | daemon drag state reset on reconnect | planned | — |
| A4 | async VirtualDesktopManager startup | planned | — |
| A5 | OverlayService mutex audit | planned | — |
| B | layout compute worker thread | planned | — |
| B-flag-removal | delete main-thread fallback | planned | — |
| C | bundled protocol hardening (apiVersion bump) | planned | — |
| D | D-Bus handler thread (conditional) | deferred | — |

## Resolved decisions

1. **LayoutSnapshot ownership: main-thread push.** The main thread constructs a fresh `LayoutSnapshot` on every layout/screen change and pushes it to `LayoutWorker` via `QueuedConnection`. The worker holds the most recent snapshot; no worker-side invalidation logic, no `QSharedDataPointer` cache layer. Fewer moving parts, no cross-thread ownership ambiguity.

2. **Protocol version enforcement: keep and enforce.** `apiVersion` in `BridgeRegistrationResult` stays. Phase C3 implementation:
   - Define `kMinCompositorApiVersion` / `kMinDaemonApiVersion` constants in `compositor-common/dbus_constants.h`.
   - `CompositorBridge::registerBridge` on the daemon side rejects effects older than `kMinCompositorApiVersion`; effect-side registration code rejects daemons older than `kMinDaemonApiVersion`.
   - On mismatch: `qCWarning` with both versions and the required minimum, plus a user-visible notification via the existing KNotification / org.freedesktop.Notifications path used elsewhere in the daemon. Notification text names the out-of-date component ("PlasmaZones effect is older than the daemon — please restart KWin after updating").
   - The rejected side disables itself cleanly (effect: no further D-Bus calls, borders cleared; daemon: leaves `CompositorBridge` unregistered and continues running, since a mismatched effect should not take the daemon down).
   - Rationale: effect and daemon can be updated independently at the packaging layer, and the current silent-marshalling-failure mode is exactly what produced the EmptyZoneList crash. A visible error beats a crash.

3. **Effect-reconnect-mid-drag test harness: build in A3.** No existing fixture. A3's PR scope expands by ~150 lines of test infra: a `QDBusServer`-backed harness that can register/unregister the daemon service name while a synthetic drag is in flight, with assertions on daemon-side state clearing. The harness is designed to be reused by Phase C regression tests.
