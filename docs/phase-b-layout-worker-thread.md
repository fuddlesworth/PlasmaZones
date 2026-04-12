# Phase B: Layout Computation Worker Thread

Status: planning
Branch: `phase-a-effect-daemon-resilience` (continues existing branch)

## Goal

Move all layout geometry computation off the Qt GUI main thread. Establish the architectural contract: layout compute is a pure function over immutable snapshots, results arrive via signals, the main thread owns Qt/QML/overlay state exclusively.

## Why now

- Today: 20-zone layouts, `recalculateZoneGeometries` is O(N) trivial math. Fast.
- Future (full WM): tree-structured layouts, constraint solvers, multi-screen coordination. N grows, cost grows.
- The main thread also runs QML overlay rendering, D-Bus dispatch, and shader animation. Any geometry stall = dropped compositor frames.
- Retrofitting threading later when every caller assumes synchronous zone access is 10x harder than establishing the pattern now.

## Thread safety analysis

`recalculateZoneGeometries` (layout.cpp:550) **mutates two things**:
1. `Layout::m_lastRecalcGeometry` — cache sentinel
2. `Zone::m_geometry` via `zone->setGeometry()` for every zone

`zone->setGeometry()` emits `geometryChanged`, which could trigger QML bindings. These writes are NOT thread-safe — no locks, no atomics, QObject signals fire on the calling thread.

**Conclusion:** We cannot call `recalculateZoneGeometries` from a worker thread against live Layout/Zone objects. A snapshot/compute/apply pattern is required.

## Design

### Architecture: snapshot → compute → apply

```
Main Thread                          Worker Thread
┌─────────────────────┐              ┌──────────────────────┐
│ 1. Build snapshot    │──────────►  │ 3. Compute results   │
│    from live Layout  │  (signal,   │    (pure function,   │
│                      │  queued)    │    no Qt objects)     │
│ 2. Continue handling │              │                      │
│    D-Bus, QML, etc.  │◄──────────  │ 4. Return results    │
│                      │  (signal,   │                      │
│ 5. Batch-apply       │  queued)    └──────────────────────┘
│    results to Zones  │
│    (beginBatchModify)│
└─────────────────────┘
```

### New types: `src/core/layoutworker/types.h`

```cpp
struct ZoneSnapshot {
    QUuid id;
    QRectF relativeGeometry;
    ZoneGeometryMode geometryMode;
    QRectF fixedGeometry;
};

struct LayoutSnapshot {
    QUuid layoutId;
    QString screenId;
    QRectF screenGeometry;
    QVector<ZoneSnapshot> zones;
};

struct ComputedZoneGeometry {
    QUuid zoneId;
    QRectF absoluteGeometry;
};

struct LayoutComputeResult {
    QUuid layoutId;
    QString screenId;
    QRectF screenGeometry;           // echo back for staleness check
    QVector<ComputedZoneGeometry> zones;
    uint64_t generation;             // for coalescing
};
```

### New class: `src/core/layoutworker/layoutworker.h`

```cpp
class LayoutWorker : public QObject {
    Q_OBJECT
public:
    explicit LayoutWorker(QObject* parent = nullptr);

public Q_SLOTS:
    void computeGeometries(const LayoutSnapshot& snapshot, uint64_t generation);

Q_SIGNALS:
    void geometriesReady(const LayoutComputeResult& result);
};
```

- `LayoutWorker` lives on a dedicated `QThread` (moved via `moveToThread`)
- `computeGeometries` is a pure function: reads only from the snapshot, produces a `LayoutComputeResult`
- No access to `Layout*`, `Zone*`, `LayoutManager`, or any QObject on the main thread
- Generation counter enables coalescing: if a newer request arrives before the current one finishes, discard the stale result

### New class: `src/core/layoutworker/layoutcomputeservice.h`

This is the main-thread coordinator — the only class other code interacts with.

```cpp
class LayoutComputeService : public QObject {
    Q_OBJECT
public:
    explicit LayoutComputeService(QObject* parent = nullptr);
    ~LayoutComputeService();

    // Request geometry computation. Snapshots the layout, sends to worker.
    // Calls back via geometriesComputed when done.
    void requestRecalculate(Layout* layout, const QString& screenId, const QRectF& screenGeometry);

    // Synchronous fallback for init-time code that cannot be async.
    // Runs computation inline on the main thread. Use sparingly.
    static void recalculateSync(Layout* layout, const QRectF& screenGeometry);

Q_SIGNALS:
    void geometriesComputed(const QString& screenId, Layout* layout);

private:
    QThread* m_thread = nullptr;
    LayoutWorker* m_worker = nullptr;
    uint64_t m_generation = 0;

    LayoutSnapshot buildSnapshot(Layout* layout, const QString& screenId, const QRectF& screenGeometry);
    void applyResult(const LayoutComputeResult& result, Layout* layout);
};
```

### Snapshot building (main thread, cheap)

`buildSnapshot` reads from live Layout/Zone objects — all const accessors:
- `zone->id()`, `zone->relativeGeometry()`, `zone->geometryMode()`, `zone->fixedGeometry()`
- `layout->id()`, `layout->zones()`, `layout->zoneCount()`

This is a shallow copy of ~300 bytes per zone. For a 100-zone layout: ~30 KB. Negligible.

### Result application (main thread, batched)

`applyResult` matches `ComputedZoneGeometry` entries by zone ID back to the live `Zone*` objects:

```
layout->beginBatchModify();
for (auto& computed : result.zones) {
    Zone* z = layout->zoneById(computed.zoneId);
    if (z) z->setGeometry(computed.absoluteGeometry);
}
layout->m_lastRecalcGeometry = result.screenGeometry;
layout->endBatchModify();
```

Signals fire once at the end (via `endBatchModify`), not per zone.

### Coalescing via generation counter

Each `requestRecalculate` increments `m_generation` and passes it to the worker. When `geometriesReady` fires, the service checks `result.generation == m_generation`. If not, the result is stale (a newer request superseded it) and is discarded. Same pattern as `m_autotileStaggerGeneration`.

Per-screen generation counters (`QHash<QString, uint64_t>`) allow independent coalescing per screen — a geometry change on screen A doesn't discard an in-flight computation for screen B.

## Migration plan

### Step 1: Create LayoutComputeService with sync-only path

- Add the new files: `types.h`, `layoutworker.h/cpp`, `layoutcomputeservice.h/cpp`
- `LayoutComputeService::requestRecalculate` initially just calls `recalculateSync` inline
- Replace all 13 call sites of `layout->recalculateZoneGeometries(geom)` with `m_computeService->requestRecalculate(layout, screenId, geom)` or `LayoutComputeService::recalculateSync(layout, geom)` for init paths
- Build + test. Behavior is identical, but the indirection is in place.

### Step 2: Replace `buildEmptyZoneList` to use pre-computed geometry

`GeometryUtils::buildEmptyZoneList` currently calls `recalculateZoneGeometries` internally. After Step 1, the geometries are already computed by the service. The helper should assert they're fresh (via `m_lastRecalcGeometry` check) and skip the recalculation, or take pre-computed zones as input.

### Step 3: Wire the worker thread

- `LayoutComputeService` constructor creates `QThread` + `LayoutWorker`, wires signals
- `requestRecalculate` snapshots and sends to worker instead of calling sync
- `geometriesReady` applies results on main thread
- Generation counter active for coalescing

### Step 4: Handle callers that need sync results

Some callers read zone geometry immediately after recalculation:
- `drag.cpp:105` — dragStarted snapped-window check (reads zone geometry for tolerance matching)
- `zonedetectionadaptor.cpp:60,165` — D-Bus query, returns immediately
- `prepareHandlerContext` — returns layout for zone detection on same tick

These callers either:
(a) Use `recalculateSync` (init paths, D-Bus queries), or
(b) Are already covered by the `m_lastRecalcGeometry` cache (drag.cpp:194), or
(c) Need a callback/continuation pattern (dragStarted snapped check)

Classify each:

| Call site | Path | Approach |
|---|---|---|
| daemon.cpp:155 | init | `recalculateSync` |
| daemon.cpp:181,205 | layout changed signal | `requestRecalculate` async → overlay updates in callback |
| start.cpp:100 | screen added | `requestRecalculate` async → overlay setup in callback |
| start.cpp:614 | VS config changed | `requestRecalculate` async → resnap in callback |
| autotile.cpp:467,480 | geometry batch | `requestRecalculate` async → retile in callback |
| drag.cpp:105 | dragStarted snapped check | `recalculateSync` (one-time, not hot) |
| drag.cpp:194 | dragMoved (hot path) | Already cached by `m_lastRecalcGeometry` — no recalc |
| drop.cpp:287 | dragStopped snap-assist | Already async via `snapAssistReady` signal (Phase C1) |
| zonedetectionadaptor.cpp:60,165 | D-Bus detection query | `recalculateSync` |
| geometryutils.cpp:487,509 | buildEmptyZoneList | Assert pre-computed (Step 2) |

### Step 5: Remove direct `recalculateZoneGeometries` from public API

- Make `Layout::recalculateZoneGeometries` private
- Only `LayoutComputeService` (friend) and `recalculateSync` can call it
- Prevents new code from accidentally computing geometry on the main thread without going through the service

## File inventory

| File | Status |
|---|---|
| `src/core/layoutworker/types.h` | New |
| `src/core/layoutworker/layoutworker.h` | New |
| `src/core/layoutworker/layoutworker.cpp` | New |
| `src/core/layoutworker/layoutcomputeservice.h` | New |
| `src/core/layoutworker/layoutcomputeservice.cpp` | New |
| `src/core/layout.h` | Modified: make recalculateZoneGeometries private, friend LayoutComputeService |
| `src/daemon/daemon.cpp` | Modified: create LayoutComputeService, replace direct calls |
| `src/daemon/daemon.h` | Modified: add LayoutComputeService member |
| `src/daemon/daemon/start.cpp` | Modified: replace direct calls |
| `src/daemon/daemon/autotile.cpp` | Modified: replace direct calls |
| `src/dbus/windowdragadaptor/drag.cpp` | Modified: replace direct calls |
| `src/dbus/windowdragadaptor/drop.cpp` | Modified: assert pre-computed |
| `src/dbus/zonedetectionadaptor.cpp` | Modified: use recalculateSync |
| `src/core/geometryutils.cpp` | Modified: assert pre-computed in buildEmptyZoneList |
| `src/CMakeLists.txt` | Modified: add new source files |
| `tests/unit/core/` | New: test_layoutworker.cpp |

## Testing

- **Unit:** `LayoutWorker::computeGeometries` with synthetic snapshots — verify pure-function contract, verify generation coalescing
- **Integration:** Daemon startup with LayoutComputeService wired, verify zones are computed
- **ThreadSanitizer:** Full daemon test suite under TSAN, verify no data races
- **Latency:** Measure p99 `requestRecalculate` → `geometriesComputed` round-trip on a 100-zone layout

## Risks

- **Signal ordering:** `geometriesComputed` arrives on the main thread via `QueuedConnection`. If a caller reads zone geometry between sending the request and receiving the result, they get stale values. Mitigated by the `m_lastRecalcGeometry` cache (most callers already check this) and the `recalculateSync` escape hatch.
- **Snapshot cost:** 30 KB per 100-zone snapshot is negligible, but if snapshot frequency reaches 60+ Hz (it shouldn't — max is 30 Hz from dragMoved, and those hit cache), revisit.
- **Init-time ordering:** Several startup paths need zones computed before proceeding. `recalculateSync` handles these.
