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

### PR 2: SnapEngine owns SnapState (state migration)

Move state ownership from WTS to SnapState — SnapEngine creates per-screen
SnapState instances and uses them for zone assignment CRUD, floating state,
pre-tile geometry, and pre-float memory. The orchestration methods
(`commitSnap`, `calculate*`, `applyBatchAssignments`) stay on WTS initially
but are refactored to accept `SnapState&` parameters instead of reading
internal maps.

1. SnapEngine creates/holds `QHash<QString, SnapState*>` keyed by screen ID
2. Redirect pure state reads (isWindowSnapped, zoneForWindow, etc.) to SnapState
3. Redirect pure state writes (assignWindowToZone, setFloating, etc.) to SnapState
4. Refactor `commitSnap` / `uncommitSnap` to operate on SnapState& instead of WTS internal maps
5. Remove duplicated state maps from WTS
6. `stateForScreen()` returns the live SnapState instance

### PR 3: Move orchestration to SnapEngine / phosphor-zones

The `calculate*` methods (calculateRotation, calculateSnapAllWindows,
calculateResnap*, calculateSnapToAppRule/EmptyZone/LastZone) are pure
functions over SnapState + LayoutRegistry + ZoneDetector. They can move
to either SnapEngine or phosphor-zones as free functions, making phosphor-zones
self-contained for snap orchestration (symmetric with phosphor-tiles).

1. Move calculate methods out of WTS
2. Move commitSnap/uncommitSnap to SnapEngine
3. WTS shrinks to: D-Bus shadow state + persistence dirty masks + pending restore queues
4. Update library-extraction-survey.md
