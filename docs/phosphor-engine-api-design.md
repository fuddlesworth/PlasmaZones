# Phosphor Engine API — Unified Placement Engine Design

**Date:** 2026-04-22
**Status:** Design proposal
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

    virtual QRect geometryForWindow(const QString& windowId) const = 0;

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
    QRect geometryForWindow(const QString& windowId) const override;
    QJsonObject toJson() const override;

    // ── Snap-specific ──
    void assignWindowToZone(const QString& windowId, const QString& zoneId,
                            const QString& screenId);
    void assignWindowToZones(const QString& windowId, const QStringList& zoneIds,
                             const QString& screenId);
    void unassignWindow(const QString& windowId);
    QString zoneForWindow(const QString& windowId) const;
    QStringList zonesForWindow(const QString& windowId) const;
    QStringList windowsInZone(const QString& zoneId) const;

    void setFloating(const QString& windowId, bool floating);
    void storePreTileGeometry(const QString& windowId, const QRect& geo,
                              const QString& screenId);
    std::optional<QRect> preTileGeometry(const QString& windowId) const;

    bool rotateWindows(bool clockwise);

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
    QString placementIdForWindow(const QString& windowId) const override;
    // Returns tiling-order position as string.
    QRect geometryForWindow(const QString& windowId) const override;
    // Returns calculated zone rect for the window's position.
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

---

## Migration sequence

### Phase 1: phosphor-engine-api (non-breaking)
1. Create `libs/phosphor-engine-api/` with `IPlacementEngine`, `IPlacementState`, `NavigationContext`
2. Move `NavigationContext` from `src/core/inavigationactions.h` to the new lib
3. Make `TilingState` implement `IPlacementState` (3 new override methods)
4. All tests pass — no behavioral change

### Phase 2: SnapState extraction (biggest phase)
1. Create `SnapState` in phosphor-zones implementing `IPlacementState`
2. Move state maps from WTS → SnapState (one map at a time, test between each)
3. Move `commitSnap`/`uncommitSnap`/`commitMultiZoneSnap` to SnapState
4. Move `calculateResnap*`, `calculateSnapAll`, `calculateRotation` to phosphor-zones as free functions or SnapState methods
5. Move auto-snap chain (`calculateSnapToAppRule`, `...EmptyZone`, `...LastZone`)
6. WTS becomes a thin coordinator that owns per-screen `SnapState` instances and delegates

### Phase 3: Unify engine interface (daemon-side)
1. Make `SnapEngine` implement `IPlacementEngine` directly (absorb `SnapNavigationAdapter`)
2. Make `AutotileEngine` implement `IPlacementEngine` directly (absorb `AutotileNavigationAdapter`)
3. Delete `IEngineLifecycle`, `INavigationActions`, `SnapNavigationAdapter`, `AutotileNavigationAdapter`
4. Simplify `ScreenModeRouter` to hold two `IPlacementEngine*`
5. Delete mode branches in daemon adaptor layer

### Phase 4: Cleanup
1. Delete now-empty WTS state members
2. Update library-extraction-survey.md
3. WTS is either deleted or becomes a thin "window shadow" store for D-Bus frame geometry / cursor / activation state

---

## Risk assessment

| Risk | Mitigation |
|---|---|
| Large refactor (~1,700 LOC moves) | Phase 2 is incremental — one state map per commit, test between each |
| SnapState serialization format change | Keep the same JSON keys — SnapState reads/writes the same format WTS did |
| D-Bus signal wiring breaks | Adaptor continues to exist — it just reads from SnapState instead of WTS |
| phosphor-zones gains new deps | Only phosphor-engine-api (INTERFACE, header-only) — no new binary deps |
| Wayfire consumer doesn't exist yet | True, but the SDK formalization is the next planned extraction and this unblocks it |
