# Phosphor Engine API — Unified Placement Engine Design

**Date:** 2026-04-22
**Status:** Implemented (PR 1 — interface layer + symmetric state)
**Motivation:** phosphor-tiles is a self-contained tiling engine; phosphor-zones is half a library. The daemon branches on mode at 20+ sites. A Wayfire plugin linking phosphor-zones today would need to reimplement all snap orchestration from scratch.

---

## Problem

### Asymmetry between phosphor-tiles and phosphor-zones

| | phosphor-tiles | phosphor-zones |
|---|---|---|
| Owns per-screen state | `TilingState` (window order, float, split, rotation, focus, serialization) | Nothing — state lives in daemon's `WindowTrackingService` |
| Owns behavior | `TilingAlgorithm` subclasses compute zones | Nothing — `SnapEngine` in daemon does zone resolution, resnap, rotation, app rules |
| Self-contained | Yes — any consumer can use it | No — consumer must reimplement 40+ methods of snap orchestration |

### Daemon still branches on mode

`ScreenModeRouter` dispatches through two separate interfaces (`IEngineLifecycle` + `INavigationActions`) but the daemon still has mode-aware wiring in the adaptor layer, persistence, and resnap paths. The interfaces live in `src/core/` (GPL, daemon-internal) rather than in a shared library.

---

## Design: `phosphor-engine-api` + symmetric state ownership

### New library: `libs/phosphor-engine-api/`

Header-only (or INTERFACE) library. Depends only on Qt6::Core. Defines the polymorphic contracts that both placement engines implement.

```
libs/phosphor-engine-api/
  include/PhosphorEngineApi/
    IPlacementEngine.h     — unified lifecycle + navigation
    IPlacementState.h      — per-screen state contract
    NavigationContext.h     — target window + screen
    PhosphorEngineApi.h    — umbrella include
  CMakeLists.txt
```

**License:** LGPL-2.1-or-later (same as other phosphor libs — third-party compositor plugins link it).

### `IPlacementEngine` — the one interface the daemon sees

Merges `IEngineLifecycle` + `INavigationActions` into a single contract. The daemon dispatches everything through this — zero mode branches.

```cpp
namespace PhosphorEngineApi {

class IPlacementEngine
{
public:
    virtual ~IPlacementEngine() = default;

    // ── Screen ownership ──
    virtual bool isActiveOnScreen(const QString& screenId) const = 0;

    // ── Window lifecycle ──
    virtual void windowOpened(const QString& windowId, const QString& screenId,
                              int minWidth = 0, int minHeight = 0) = 0;
    virtual void windowClosed(const QString& windowId) = 0;
    virtual void windowFocused(const QString& windowId, const QString& screenId) = 0;

    // ── Navigation (user intents) ──
    virtual void focusInDirection(const QString& direction, const NavigationContext& ctx) = 0;
    virtual void moveFocusedInDirection(const QString& direction, const NavigationContext& ctx) = 0;
    virtual void swapFocusedInDirection(const QString& direction, const NavigationContext& ctx) = 0;
    virtual void moveFocusedToPosition(int position, const NavigationContext& ctx) = 0;
    virtual void rotateWindows(bool clockwise, const NavigationContext& ctx) = 0;
    virtual void reapplyLayout(const NavigationContext& ctx) = 0;
    virtual void snapAllWindows(const NavigationContext& ctx) = 0;
    virtual void cycleFocus(bool forward, const NavigationContext& ctx) = 0;
    virtual void pushToEmptyZone(const NavigationContext& ctx) = 0;
    virtual void restoreFocusedWindow(const NavigationContext& ctx) = 0;

    // ── Float management ──
    virtual void toggleWindowFloat(const QString& windowId, const QString& screenId) = 0;
    virtual void setWindowFloat(const QString& windowId, bool shouldFloat) = 0;

    // ── Persistence ──
    virtual void saveState() = 0;
    virtual void loadState() = 0;

    // ── State access ──
    virtual IPlacementState* stateForScreen(const QString& screenId) = 0;
    virtual const IPlacementState* stateForScreen(const QString& screenId) const = 0;
};

} // namespace PhosphorEngineApi
```

### `IPlacementState` — per-screen state contract

Both `TilingState` and the new `SnapState` implement this. The daemon's persistence layer, D-Bus adaptor, and compositor plugin all consume it uniformly.

```cpp
namespace PhosphorEngineApi {

class IPlacementState
{
public:
    virtual ~IPlacementState() = default;

    virtual QString screenId() const = 0;

    // ── Window queries ──
    virtual int windowCount() const = 0;
    virtual QStringList managedWindows() const = 0;
    virtual bool containsWindow(const QString& windowId) const = 0;

    // ── Float state ──
    virtual bool isFloating(const QString& windowId) const = 0;
    virtual QStringList floatingWindows() const = 0;

    // ── Placement queries ──
    virtual QString placementIdForWindow(const QString& windowId) const = 0;
    // Snap: returns zone UUID. Autotile: returns tiling-order position string.
    // Opaque to the daemon — engines interpret it.

    // ── Serialization ──
    virtual QJsonObject toJson() const = 0;
};

} // namespace PhosphorEngineApi
```

---

## Symmetric state ownership

### phosphor-zones gets `SnapState` (new, analogous to `TilingState`)

Extracts the snap-specific state from `WindowTrackingService` into phosphor-zones:

| WTS member → SnapState member | Description |
|---|---|
| `m_windowZoneAssignments` | window → zone UUID(s) |
| `m_windowScreenAssignments` | window → screen |
| `m_windowDesktopAssignments` | window → virtual desktop |
| `m_preTileGeometries` | window → pre-snap geometry |
| `m_floatingWindows` | floating window set |
| `m_autotileFloatedWindows` | subset: floated by autotile transition |
| `m_savedSnapFloatingWindows` | subset: preserved float state |
| `m_preFloatZoneAssignments` | pre-float zone memory |
| `m_preFloatScreenAssignments` | pre-float screen memory |
| `m_userSnappedClasses` | app classes that user has manually snapped |
| `m_autoSnappedWindows` | auto-snapped window set |
| `m_windowStickyStates` | per-window sticky (all-desktops) state |
| `m_pendingRestoreQueues` | FIFO session restore queues |

```cpp
namespace PhosphorZones {

class PHOSPHORZONES_EXPORT SnapState : public QObject, public PhosphorEngineApi::IPlacementState
{
    Q_OBJECT
public:
    explicit SnapState(const QString& screenId, QObject* parent = nullptr);

    // ── IPlacementState ──
    QString screenId() const override;
    int windowCount() const override;
    QStringList managedWindows() const override;
    bool containsWindow(const QString& windowId) const override;
    bool isFloating(const QString& windowId) const override;
    QStringList floatingWindows() const override;
    QString placementIdForWindow(const QString& windowId) const override; // zone UUID
    QJsonObject toJson() const override;

    // ── Snap-specific ──
    void assignWindowToZone(const QString& windowId, const QString& zoneId);
    void assignWindowToZones(const QString& windowId, const QStringList& zoneIds);
    void unassignWindow(const QString& windowId);
    QString zoneForWindow(const QString& windowId) const;
    QStringList zonesForWindow(const QString& windowId) const;
    QStringList windowsInZone(const QString& zoneId) const;

    void setFloating(const QString& windowId, bool floating);
    void storePreTileGeometry(const QString& windowId, const QRect& geometry,
                              const QString& connectorName = {},
                              bool overwrite = false);
    std::optional<QRect> preTileGeometry(const QString& windowId) const;

    QStringList rotateAssignments(bool clockwise);

    static SnapState* fromJson(const QJsonObject& json, QObject* parent = nullptr);

Q_SIGNALS:
    void windowAssigned(const QString& windowId, const QString& zoneId);
    void windowUnassigned(const QString& windowId);
    void floatingChanged(const QString& windowId, bool floating);
    void stateChanged();
};

} // namespace PhosphorZones
```

### phosphor-tiles' `TilingState` also implements `IPlacementState`

```cpp
class PHOSPHORTILES_EXPORT TilingState : public QObject,
                                         public PhosphorEngineApi::IPlacementState
{
    // Existing API unchanged.
    // New overrides:
    QStringList managedWindows() const override;
    // Returns windowOrder().
    QString placementIdForWindow(const QString& windowId) const override;
    // Returns tiling-order position as string.
};
```

---

## What moves where

### Into phosphor-zones (from WTS)

| What | LOC est. | Notes |
|---|---|---|
| `SnapState` class (state maps + CRUD) | ~600 | New file, extracted from WTS m_ members |
| `commitSnap` / `uncommitSnap` / `commitMultiZoneSnap` | ~200 | State mutation — belongs with the state |
| `resolveUnfloatGeometry` | ~80 | Zone geometry resolution |
| `calculateResnap*` family (4 methods) | ~300 | Pure functions over SnapState + LayoutRegistry + ZoneDetector |
| `calculateSnapAll` / `calculateRotation` | ~200 | Pure functions over SnapState |
| `calculateSnapToAppRule` / `...EmptyZone` / `...LastZone` | ~200 | Auto-snap fallback chain |
| `applyBatchAssignments` | ~100 | Batch state update |

**Total: ~1,700 LOC moves from WTS → phosphor-zones**

### Into phosphor-engine-api (new)

| What | LOC est. |
|---|---|
| `IPlacementEngine` | ~80 |
| `IPlacementState` | ~50 |
| `NavigationContext` (from `src/core/inavigationactions.h`) | ~20 |

**Total: ~150 LOC, header-only**

### Stays in daemon (WTS becomes thin)

| What | Notes |
|---|---|
| D-Bus shadow state (frame geometry, cursor screen, active window) | Compositor-specific |
| Persistence orchestration (dirty masks, debounced writes) | I/O policy |
| D-Bus adaptor (WindowTrackingAdaptor) | D-Bus facade |
| Session restore queue consumption | Daemon startup sequence |

### ScreenModeRouter simplifies

```cpp
// Before: stores SnapEngine*, AutotileEngine*, two INavigationActions*
// After: stores two IPlacementEngine*

class ScreenModeRouter
{
public:
    ScreenModeRouter(IPlacementEngine* snapEngine, IPlacementEngine* autotileEngine,
                     PhosphorZones::LayoutRegistry* layouts);

    IPlacementEngine* engineFor(const QString& screenId) const;
    // navigatorFor() is gone — IPlacementEngine IS the navigator.
};
```

---

## Dependency graph after migration

```
phosphor-engine-api (INTERFACE, LGPL)
  IPlacementEngine, IPlacementState, NavigationContext
       │                    │
       ▼                    ▼
phosphor-zones           phosphor-tiles
  SnapState ────────┐   TilingState ────────┐
  (implements       │   (implements         │
   IPlacementState) │    IPlacementState)   │
  LayoutRegistry    │   AlgorithmRegistry   │
  ZoneDetector      │   TilingAlgorithm     │
  snap orchestration│                       │
       │            │        │              │
       ▼            │        ▼              │
  ┌────────────────────────────────────┐    │
  │        PlasmaZones daemon          │    │
  │                                    │    │
  │  SnapEngine : IPlacementEngine ◄───┘    │
  │  AutotileEngine : IPlacementEngine ◄────┘
  │      (both thin orchestrators)     │
  │                                    │
  │  ScreenModeRouter                  │
  │    engineFor(screenId)->foo()      │
  │    (zero mode branches)            │
  │                                    │
  │  WindowTrackingAdaptor             │
  │    (D-Bus, persistence, shadows)   │
  └────────────────────────────────────┘
```

**Key properties:**
- phosphor-zones and phosphor-tiles depend on phosphor-engine-api (INTERFACE, no binary)
- phosphor-zones and phosphor-tiles have zero cross-references (unchanged)
- Daemon sees only `IPlacementEngine*` — never branches on mode
- Wayfire plugin links phosphor-zones + phosphor-engine-api and gets full snap functionality
- `IPlacementState` lets persistence and D-Bus adaptor serialize uniformly

**Interface width note:** `IPlacementEngine` has 22 pure virtual methods (lifecycle + navigation + persistence + state access). With only two implementors this is a pragmatic trade-off: the merged interface eliminates the adapter indirection that was the old `IEngineLifecycle` + `INavigationActions` split. If a third engine type is added (e.g., floating-only mode), consider splitting the navigation methods into a separate mixin.

---

## Migration sequence

### PR 1: Interface layer + symmetric state (THIS PR) — DONE

1. Created `libs/phosphor-engine-api/` with `IPlacementEngine`, `IPlacementState`, `NavigationContext`
2. `TilingState` implements `IPlacementState` (2 new methods + 6 overrides)
3. Created `SnapState` in phosphor-zones implementing `IPlacementState`
4. Both engines implement `IPlacementEngine` directly
5. Deleted `IEngineLifecycle`, `INavigationActions`, `SnapNavigationAdapter`, `AutotileNavigationAdapter` (6 files, -614 net lines)
6. `ScreenModeRouter` holds two `IPlacementEngine*`, daemon never branches on mode

### PR 2: SnapEngine owns SnapState — DONE

SnapState wired to SnapEngine for pure state reads/writes. Dual-store
with WTS propagating mutations.

### PR 3: Move orchestration + collapse dual-store — DONE

1. commitSnap/commitMultiZoneSnap/uncommitSnap/applyBatchAssignments → SnapEngine
2. 9 calculate* methods → SnapEngine
3. resolveUnfloatGeometry → SnapEngine
4. Dual-store collapsed: 9 WTS member variables removed, SnapState is single source of truth
5. WTS shrinks to: cross-mode state + persistence + geometry helpers

### PR 4: Engine contract redesign — unified window state model

---

## PR 4: Engine Contract Redesign

**Goal:** Make `IPlacementEngine` a complete, self-contained contract that any
third-party can implement to create a new window management engine (snap zones,
autotile, i3-tree, paperwm-scroll, floating-only, etc.).

### Window State Model

A window has exactly 3 states relative to an engine:

| State | Who controls geometry | Who remembers |
|-------|----------------------|---------------|
| **Unmanaged** | User / compositor | Nobody — natural geometry |
| **Engine-owned** | Engine decides placement | Engine owns placement data |
| **Floated** | User / compositor | Engine remembers prior placement for unfloat |

State transitions:

```
                    ┌──────────────┐
                    │  Unmanaged   │
                    └──────┬───────┘
                           │ engine claims window
                           │ (captures unmanaged geometry)
                           ▼
                    ┌──────────────┐
          ┌────────│ Engine-owned  │────────┐
          │        └──────────────┘        │
          │ float                   restore │
          │ (engine remembers       (engine │
          │  placement)             restores│
          ▼                        geometry)│
    ┌──────────┐                           │
    │ Floated  │───────────────────────────┘
    └──────────┘ unfloat
                 (engine restores to
                  remembered placement)
```

### Base class + engine hooks

Universal float/unfloat mechanics are the same for every engine. Engines
should not reimplement geometry save/restore — only placement logic.

```
┌─────────────────────────────────────────────────────────┐
│              IPlacementEngine (interface)                │
│  lifecycle + navigation + state access (existing)       │
└─────────────────────┬───────────────────────────────────┘
                      │
┌─────────────────────▼───────────────────────────────────┐
│           PlacementEngineBase (abstract base)            │
│                                                         │
│  UNIVERSAL MECHANICS (final — engines don't override):  │
│  • claimWindow(id, geo)     Unmanaged → EngineOwned     │
│  • releaseWindow(id)        EngineOwned → Unmanaged     │
│  • floatWindow(id)          EngineOwned → Floated        │
│  • unfloatWindow(id)        Floated → EngineOwned        │
│  • windowState(id)          query current state          │
│  • unmanagedGeometry(id)    geometry before any engine   │
│  • serializeBaseState()     persist universal state      │
│  • deserializeBaseState()   restore universal state      │
│                                                         │
│  INTERNAL STATE (owned by base):                        │
│  • m_unmanagedGeometries    QHash<id, QRect+screen>     │
│  • m_windowStates           QHash<id, WindowState>      │
│                                                         │
│  ENGINE HOOKS (abstract — each engine implements):      │
│  • onWindowClaimed(id)      engine decides placement     │
│  • onWindowReleased(id)     engine cleans up placement   │
│  • onWindowFloated(id)      engine saves placement data  │
│  • onWindowUnfloated(id)    engine restores placement    │
│  • placementGeometry(id)    current engine geometry      │
│  • serializeEngineState()   engine-specific persistence  │
│  • deserializeEngineState() engine-specific restore      │
└─────────────────────────────────────────────────────────┘
         │                              │
┌────────▼────────┐           ┌────────▼────────┐
│   SnapEngine    │           │ AutotileEngine  │
│                 │           │                 │
│ onWindowClaimed │           │ onWindowClaimed │
│  → zone assign  │           │  → tile insert  │
│ onWindowFloated │           │ onWindowFloated │
│  → save zone    │           │  → save position│
│ onWindowUnfloat │           │ onWindowUnfloat │
│  → restore zone │           │  → restore pos  │
│                 │           │                 │
│ Owns: SnapState │           │ Owns: TilingState│
└─────────────────┘           └─────────────────┘
```

#### PlacementEngineBase implementation

```cpp
namespace PhosphorEngineApi {

enum class WindowState { Unmanaged, EngineOwned, Floated };

class PlacementEngineBase : public QObject, public IPlacementEngine
{
    Q_OBJECT

public:
    // ── Universal mechanics (final — engines don't touch) ──

    void claimWindow(const QString& windowId, const QRect& currentGeometry) final
    {
        m_unmanagedGeometries[windowId] = {currentGeometry, screenId};
        m_windowStates[windowId] = WindowState::EngineOwned;
        onWindowClaimed(windowId);
    }

    void releaseWindow(const QString& windowId) final
    {
        onWindowReleased(windowId);
        m_windowStates.remove(windowId);
        // Caller restores unmanagedGeometry via signal
    }

    void floatWindow(const QString& windowId) final
    {
        Q_ASSERT(m_windowStates.value(windowId) == WindowState::EngineOwned);
        onWindowFloated(windowId); // engine saves placement
        m_windowStates[windowId] = WindowState::Floated;
    }

    void unfloatWindow(const QString& windowId) final
    {
        Q_ASSERT(m_windowStates.value(windowId) == WindowState::Floated);
        m_windowStates[windowId] = WindowState::EngineOwned;
        onWindowUnfloated(windowId); // engine restores placement
    }

    WindowState windowState(const QString& windowId) const final
    {
        return m_windowStates.value(windowId, WindowState::Unmanaged);
    }

    QRect unmanagedGeometry(const QString& windowId) const final
    {
        return m_unmanagedGeometries.value(windowId).geometry;
    }

protected:
    // ── Engine hooks (abstract — each engine implements) ──

    virtual void onWindowClaimed(const QString& windowId) = 0;
    virtual void onWindowReleased(const QString& windowId) = 0;
    virtual void onWindowFloated(const QString& windowId) = 0;
    virtual void onWindowUnfloated(const QString& windowId) = 0;

    // Engine-specific geometry for the current placement
    virtual QRect placementGeometry(const QString& windowId) const = 0;

    // Engine-specific serialization
    virtual QJsonObject serializeEngineState() const = 0;
    virtual void deserializeEngineState(const QJsonObject& state) = 0;

Q_SIGNALS:
    void applyGeometryRequested(const QString& windowId, const QRect& geometry,
                                const QString& screenId);

private:
    struct UnmanagedEntry { QRect geometry; QString screenId; };
    QHash<QString, UnmanagedEntry> m_unmanagedGeometries;
    QHash<QString, WindowState> m_windowStates;
};

} // namespace PhosphorEngineApi
```

#### What a third-party engine implements

A new engine (e.g., PaperWM-style horizontal scroll):

```cpp
class ScrollEngine : public PhosphorEngineApi::PlacementEngineBase
{
protected:
    void onWindowClaimed(const QString& windowId) override
    {
        m_windowOrder.append(windowId);
        recalculateViewport();
    }
    void onWindowReleased(const QString& windowId) override
    {
        m_windowOrder.removeAll(windowId);
        recalculateViewport();
    }
    void onWindowFloated(const QString& windowId) override
    {
        m_savedPositions[windowId] = m_windowOrder.indexOf(windowId);
        m_windowOrder.removeAll(windowId);
    }
    void onWindowUnfloated(const QString& windowId) override
    {
        int pos = m_savedPositions.take(windowId);
        m_windowOrder.insert(pos, windowId);
        recalculateViewport();
    }

private:
    QStringList m_windowOrder;
    QHash<QString, int> m_savedPositions;
};
```

No geometry save/restore boilerplate. No float FSM. Just placement logic.

### What the daemon provides (not the engine)

The daemon is the coordinator. It provides:

| Responsibility | Component |
|---------------|-----------|
| Screen topology | `ScreenManager` |
| Which engine owns which screen | `ScreenModeRouter` |
| Floating flag (cross-mode) | `WindowTrackingService` |
| Sticky state (cross-mode) | `WindowTrackingService` |
| D-Bus facade | `WindowTrackingAdaptor` |
| Persistence (save/load) | `WindowTrackingAdaptor` |
| Mode transitions | Daemon signal handlers |

**WTS does NOT own:**
- Zone assignments (engine-specific → SnapState)
- Tiling order (engine-specific → TilingState)
- Pre-tile geometry (engine responsibility → IPlacementEngine)
- Pre-float zones/screen (engine responsibility → IPlacementEngine)
- Auto-snap bookkeeping (engine responsibility → SnapEngine)
- Session restore queues (engine responsibility → each engine)

### What moves where

| Current location | Current name | Target | New concept |
|-----------------|-------------|--------|-------------|
| WTS `m_preTileGeometries` | "pre-tile geometry" | Each engine | `unmanagedGeometry` — captured on claim |
| WTS `m_preFloatZoneAssignments` | "pre-float zones" | SnapState | Engine-internal float restore data |
| WTS `m_preFloatScreenAssignments` | "pre-float screen" | SnapState | Engine-internal float restore data |
| WTS `m_floatingWindows` | "floating windows" | WTS (stays) | Cross-mode flag: "window is floated" |
| WTS `m_autotileFloatedWindows` | "autotile-floated" | AutotileEngine | Engine-local transition state |
| WTS `m_savedSnapFloatingWindows` | "saved snap floating" | Daemon signals | Mode-transition bookkeeping |
| WTS `m_pendingRestoreQueues` | "pending restore" | SnapEngine | Engine-owned session restore |
| WTS `m_effectReportedWindows` | "effect reported" | SnapEngine | Engine-owned runtime flag |

### WTS after PR 4

WTS becomes a thin cross-mode service:

```cpp
class WindowTrackingService {
    // Cross-mode state (used by ALL engines)
    bool isWindowFloating(const QString& windowId) const;
    void setWindowFloating(const QString& windowId, bool floating);
    bool isWindowSticky(const QString& windowId) const;
    void setWindowSticky(const QString& windowId, bool sticky);

    // Geometry helpers (shared infrastructure)
    QRect zoneGeometry(const QString& zoneId, const QString& screenId) const;
    QRect resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const;
    QString resolveEffectiveScreenId(const QString& screenId) const;
    QString currentAppIdFor(const QString& windowId) const;

    // Persistence coordination
    void markDirty(DirtyMask fields);
    DirtyMask takeDirty();
};
```

Everything else moves to the engines. Each engine is self-contained:
- Creates its own state object (SnapState / TilingState) internally
- Owns all placement logic
- Owns its own unmanaged-geometry cache
- Owns its own session restore queues
- Serializes/deserializes its own state

### Third-party engine contract

A new engine needs to:

1. Subclass `PlacementEngineBase` (gets universal mechanics for free)
2. Implement the 4 hooks: `onWindowClaimed`, `onWindowReleased`, `onWindowFloated`, `onWindowUnfloated`
3. Implement `IPlacementState` for per-screen D-Bus queries
4. Implement `serializeEngineState()` / `deserializeEngineState()`
5. Register with `ScreenModeRouter` via a new `AssignmentEntry::Mode` value

The daemon doesn't branch on engine type. `ScreenModeRouter::engineFor(screenId)`
returns the right `IPlacementEngine*` and every call goes through the base class.

### Migration sequence

Phase 1: Create PlacementEngineBase
1. Create `libs/phosphor-engine-api/PlacementEngineBase.h` with universal mechanics
2. SnapEngine + AutotileEngine inherit from `PlacementEngineBase` instead of raw `IPlacementEngine`
3. Move unmanaged geometry cache from WTS `m_preTileGeometries` → base class `m_unmanagedGeometries`

Phase 2: Move engine-specific state out of WTS
4. Move `m_preFloatZoneAssignments` + `m_preFloatScreenAssignments` → SnapState (engine-internal float restore)
5. Move `m_pendingRestoreQueues` + `m_effectReportedWindows` → SnapEngine
6. Move `m_autotileFloatedWindows` → AutotileEngine
7. Move `m_savedSnapFloatingWindows` → daemon mode-transition handler

Phase 3: Engine symmetry
8. SnapEngine creates SnapState in constructor (symmetric with AutotileEngine/TilingState)
9. Each engine implements `serializeEngineState()` / `deserializeEngineState()`
10. WTS shrinks to: floating flag + sticky flag + geometry helpers + dirty mask

### Persistence impact

Session persistence currently lives in `WindowTrackingAdaptor` (KConfig).
After PR 4, each engine serializes its own state. WTA calls
`engine->serializeEngineState()` and writes the result to its per-engine
KConfig section. On load, WTA reads the section and calls
`engine->deserializeEngineState()`. The base class handles
`serializeBaseState()` / `deserializeBaseState()` for the universal
unmanaged-geometry cache.

No config schema migration needed — the KConfig format is already
per-field (zones, screens, desktops, preTile, etc.). The fields just
move from WTA-managed to engine-managed, but the on-disk keys stay the
same. A version bump in `ConfigSchemaVersion` documents the ownership
change for auditability but the actual serialized data is compatible.
