# Phase E: Persistence I/O Thread + Autotile Compute Thread

Status: planning
Branch: `phase-a-effect-daemon-resilience`

## E1: Persistence I/O Thread

### Problem
`saveState()` runs on the main thread every 500ms (debounced). It reads 4 live `QHash` maps from `WindowTrackingService`, serializes to JSON, and writes to disk via `QSaveFile`. On a 50-window desktop, serialization + atomic file write blocks the event loop.

### Design
```
Main Thread                          I/O Thread
┌─────────────────────┐              ┌──────────────────────┐
│ Timer fires (500ms) │              │                      │
│ 1. Deep-copy hashes │──────────►  │ 2. Serialize JSON    │
│    into snapshot    │  (signal)    │ 3. QSaveFile write   │
│ 2. Return to event  │              │ 4. Signal completion │
│    loop immediately │◄──────────  │                      │
└─────────────────────┘              └──────────────────────┘
```

- Snapshot is a deep copy of 4 QHash maps into value types
- Worker serializes + writes (the expensive part)
- Main thread never blocks on I/O
- Shutdown path: final save is synchronous (must complete before exit)

### Files
- `src/dbus/windowtrackingadaptor/persistenceworker.h/cpp` — new
- `src/dbus/windowtrackingadaptor/saveload.cpp` — modify save path

## E2: Autotile Compute Thread

### Problem
`AutotileEngine::recalculateLayout()` runs tiling algorithms synchronously. Currently fast (simple algorithms, small window counts), but will grow as algorithms get more complex (constraint solvers, tree rebalancing) and PlasmaZones moves toward full WM.

### Key insight
`TilingAlgorithm::calculateZones()` is already a **pure const method** that takes `TilingParams` (a POD struct) and returns `QVector<QRect>`. The API is already thread-safe by design — no refactoring of the algorithm interface needed.

### Design
```
Main Thread                          Compute Thread
┌─────────────────────┐              ┌──────────────────────┐
│ retileScreen()      │              │                      │
│ 1. Build TilingParams│─────────►  │ 2. algo->calculate   │
│ 2. Continue event   │  (signal)   │    Zones(params)     │
│    loop             │              │ 3. Return zones      │
│ 3. applyTiling()    │◄──────────  │                      │
│    (emit D-Bus)     │              └──────────────────────┘
└─────────────────────┘
```

- `TilingParams` already exists as the input snapshot
- Algorithm `calculateZones()` is already stateless + const
- `applyTiling()` stays on main thread (writes TilingState, emits D-Bus)
- Generation counter per screen for coalescing

### Files
- `src/autotile/autotileworker.h/cpp` — new
- `src/autotile/AutotileEngine.h/cpp` — modify retile path
