# Phosphor Library Architecture Analysis

Date: 2026-04-24
Branch: feat/library-architecture-restructuring
Status: Planning document for library extraction and restructuring

---

## 1. Current State: Dependency Graph

### 1.1 The 15 Libraries

| Library | Type | Qt Deps | Phosphor Deps (PUBLIC) | Phosphor Deps (PRIVATE) |
|---------|------|---------|------------------------|-------------------------|
| phosphor-identity | INTERFACE | Core | -- | -- |
| phosphor-config | SHARED | Core, Gui | -- | -- |
| phosphor-layout-api | SHARED | Core | -- | -- |
| phosphor-engine-api | SHARED | Core, Gui | -- | -- |
| phosphor-protocol | SHARED | Core, DBus | -- | -- |
| phosphor-animation | SHARED | Core, Gui | -- | -- |
| phosphor-audio | SHARED | Core | -- | -- |
| phosphor-shortcuts | SHARED | Core, Gui | -- | DBus, KF6::GlobalAccel (optional) |
| phosphor-shell | SHARED | Core, Gui | -- | GuiPrivate, WaylandClientPrivate, ShaderToolsPrivate, Wayland::Client |
| phosphor-rendering | SHARED | Core, Gui, Quick | phosphor-shell | GuiPrivate, ShaderToolsPrivate |
| phosphor-screens | SHARED | Core, Gui, DBus | phosphor-identity | phosphor-shell |
| phosphor-layer | SHARED | Core, Gui, Qml, Quick | phosphor-shell (conditional) | -- |
| phosphor-surfaces | SHARED | Core, Quick | phosphor-layer | -- |
| phosphor-tiles | SHARED | Core, Qml | phosphor-layout-api, phosphor-engine-api | -- |
| phosphor-zones | SHARED | Core, Gui | phosphor-layout-api, phosphor-engine-api, phosphor-config | phosphor-identity, phosphor-screens |

### 1.2 Dependency Diagram (Current)

```
                            ┌─────────────────────────┐
                            │     Qt6::Core only       │
                            └─────────┬───────────────-┘
                                      │
            ┌─────────────────────────┼─────────────────────────────┐
            │                         │                             │
     ┌──────┴───────┐        ┌───────┴────────┐           ┌───────┴────────┐
     │   identity   │        │  layout-api    │           │  engine-api    │
     │  (INTERFACE)  │        │  Qt:Core       │           │  Qt:Core+Gui   │
     │  Qt:Core      │        └───────┬────────┘           └──┬─────┬──────┘
     └──┬───────┬───┘                 │                       │     │
        │       │          ┌──────────┼───────────────────────┘     │
        │       │          │          │                             │
        │       │   ┌──────┴──────┐   │   ┌────────────────────────┘
        │       │   │    tiles    │   │   │
        │       │   │  Qt:Core+Qml│   │   │
        │       │   └─────────────┘   │   │
        │       │                     │   │
        │  ┌────┴──────────┐    ┌─────┴───┴────────────────────────────┐
        │  │   screens     │    │             zones                     │
        │  │ Qt:Core+Gui+  │    │  Qt:Core+Gui                        │
        │  │     DBus       │    │  PUBLIC: layout-api, engine-api,     │
        │  └───────────────┘    │          config                      │
        │                       │  PRIVATE: identity, screens          │
        │                       └──────────────────────────────────────┘
        │
 ┌──────┴──────────┐     ┌──────────────┐     ┌─────────────┐
 │   config        │     │  protocol    │     │  animation  │
 │  Qt:Core+Gui    │     │ Qt:Core+DBus │     │ Qt:Core+Gui │
 └─────────────────┘     └──────────────┘     └─────────────┘

 ┌──────────────┐     ┌───────────────┐     ┌──────────────┐
 │   audio      │     │  shortcuts    │     │    shell     │
 │  Qt:Core     │     │ Qt:Core+Gui   │     │ Qt:Core+Gui  │
 └──────────────┘     │ (DBus PRIV)   │     │ (Wayland PRIV│
                      └───────────────┘     └──────┬───────┘
                                                   │
                                       ┌───────────┼───────────┐
                                       │           │           │
                                ┌──────┴────┐ ┌────┴───┐ ┌────┴──────┐
                                │ rendering │ │ layer  │ │ screens   │
                                │ Qt:Quick  │ │Qt:Quick│ │(PRIVATE)  │
                                └───────────┘ └────┬───┘ └───────────┘
                                                   │
                                              ┌────┴──────┐
                                              │ surfaces  │
                                              │ Qt:Quick  │
                                              └───────────┘
```

### 1.3 GPL Daemon Layer (plasmazones_core)

The daemon (`plasmazones_core`) links ALL 15 libraries publicly:

```
plasmazones_core (GPL, SHARED)
  PUBLIC:
    PhosphorConfig, PhosphorEngineApi, PhosphorIdentity,
    PhosphorLayoutApi, PhosphorProtocol, PhosphorScreens,
    PhosphorShell, PhosphorTiles, PhosphorZones,
    plasmazones_compositor_common
  PRIVATE:
    Wayland::Client, Qt6::Concurrent, Qt6::WaylandClientPrivate
```

Two placement engines live INSIDE plasmazones_core:
- `src/autotile/AutotileEngine.{h,cpp}` + 5 supporting files
- `src/snap/SnapEngine.{h,cpp}` + 6 supporting files

### 1.4 Compositor Plugin Layer (kwin-effect)

The KWin effect links:
- `plasmazones_compositor_common` (STATIC, GPL)
  - which transitively pulls: PhosphorProtocol, PhosphorIdentity, PhosphorAnimation
- PhosphorProtocol (explicit)
- PhosphorAnimation (explicit)
- KWin::kwin

### 1.5 Settings/Editor Layer

Both `plasmazones-settings` and `plasmazones-editor` link `plasmazones_core` and thus
transitively inherit all 15 libraries. The editor additionally links `plasmazones_rendering`
(which adds PhosphorRendering). The settings app additionally links KF6::Kirigami.

---

## 2. Problems

### 2.1 phosphor-engine-api Is a Grab-Bag

phosphor-engine-api currently contains **four distinct concerns** that have accumulated
over the PR 5-7 extraction sequence:

1. **Engine contract**: IPlacementEngine, IPlacementState, PlacementEngineBase,
   NavigationContext -- the actual engine interface.
2. **Service interfaces**: IWindowTrackingService, IVirtualDesktopManager --
   these are daemon-level service contracts that engines CONSUME, not part of
   the engine's own API.
3. **Geometry utilities**: GeometryUtils (coordinate transforms, overlap removal,
   min-size enforcement, JSON serialization) -- a stateless math library.
4. **Settings contract + types**: IGeometrySettings, PerScreenSnappingKey,
   GeometryDefaults, PerScreenKeys, JsonKeys -- configuration plumbing.
5. **Domain value types**: EngineTypes.h (TilingStateKey, SnapIntent, ResnapEntry,
   PendingRestore, SnapResult, UnfloatResult, ZoneAssignmentEntry) -- shared
   data structures.

This is a problem because:
- A third-party engine developer who wants to implement IPlacementEngine must link
  phosphor-engine-api, which carries IWindowTrackingService -- a 135-line interface
  that forward-declares `PhosphorZones::Layout*` and `Phosphor::Screens::ScreenManager*`.
  Those are daemon internals that an out-of-tree engine should never see.
- GeometryUtils and EngineTypes are duplicated: phosphor-zones has its own
  `PhosphorZones::GeometryUtils` namespace that `using`-imports from
  `PhosphorEngineApi::GeometryUtils` and adds zone-specific functions. This
  dual-namespace approach creates confusion about where to add new geometry functions.
- IGeometrySettings carries per-screen snapping keys that are snap-mode-specific,
  yet it lives in the "universal" engine API.

### 2.2 IWindowTrackingService Forward-Declares Concrete Types

`IWindowTrackingService.h` contains:
```cpp
namespace PhosphorZones { class Layout; }
namespace Phosphor::Screens { class ScreenManager; }
```

And methods like:
```cpp
virtual QString findEmptyZoneInLayout(PhosphorZones::Layout* layout, ...) const = 0;
virtual Phosphor::Screens::ScreenManager* screenManager() const = 0;
```

This means phosphor-engine-api conceptually depends on phosphor-zones and
phosphor-screens even though no CMake link exists. Any third-party engine that
receives an `IWindowTrackingService*` would see `PhosphorZones::Layout*` in the
interface. This is snap-mode-specific leakage into the universal engine contract.

### 2.3 phosphor-zones Is Doing Double Duty

phosphor-zones contains:
- **Zone domain model**: Zone, Layout, ZoneDetector, ZoneHighlighter, LayoutUtils,
  LayoutRegistry, ZonesLayoutSource -- the zone primitives.
- **Snap engine state**: SnapState (implements IPlacementState) -- this is
  engine-runtime state, not a zone primitive.
- **Geometry utilities**: GeometryUtils (zone-specific gap calculations, screen
  geometry resolution) -- a bridge between zones and engine-api geometry.

SnapState belongs with the SnapEngine, not with the zone domain model. A Wayfire
plugin that only needs to load zone layouts and render zone overlays should not
carry SnapState in its link closure.

### 2.4 Both Domain Libraries Depend on engine-api (Upward Coupling)

phosphor-zones and phosphor-tiles both have PUBLIC dependencies on phosphor-engine-api:
- phosphor-zones: because SnapState implements IPlacementState, and
  GeometryUtils.h includes IGeometrySettings
- phosphor-tiles: because TilingState presumably also interacts with engine-api types

This means the "domain" layer (zones, tiles) depends on the "engine contract" layer.
In a clean architecture, it should be the reverse: engines depend on domain models,
not domain models depending on engine contracts. The coupling exists because
IPlacementState was placed in engine-api and both SnapState and TilingState
implement it.

### 2.5 The Third-Party Engine Link Surface Is Too Large

Today, a developer building a new placement engine needs:
1. phosphor-engine-api (the contract) -- brings Qt6::Core, Qt6::Gui
2. ...which exposes IWindowTrackingService -- which forward-declares PhosphorZones::Layout*
3. ...and EngineTypes.h carries TilingStateKey, SnapResult, etc. -- concepts from
   both existing engines that a third engine may not need

There is no "engine SDK" subset. The engine developer gets the union of both engines'
type requirements.

### 2.6 GeometryUtils Scattered Across Three Locations

Geometry utility functions live in:
1. `PhosphorEngineApi::GeometryUtils` (libs/phosphor-engine-api) -- coordinate transforms,
   overlap removal, serialization
2. `PhosphorZones::GeometryUtils` (libs/phosphor-zones) -- zone gap calculations,
   screen geometry resolution, re-exports engine-api functions via `using`
3. `src/core/geometryutils.{h,cpp}` (daemon) -- presumably more geometry code

This three-way split makes it unclear where new geometry functions belong.

### 2.7 No Circular Dependencies (Good)

The current graph has no cycles. This is a solid foundation.

### 2.8 phosphor-shell Pulls Heavy Wayland Dependencies

phosphor-shell requires Wayland::Client, Qt6::WaylandClientPrivate,
Qt6::ShaderToolsPrivate, Qt6::GuiPrivate. It is correctly kept as a PRIVATE
dependency of phosphor-screens, but phosphor-layer and phosphor-rendering
both depend on it PUBLIC, which means any consumer of those libraries inherits
the full Wayland build dependency chain. This is correct for their use case
(they DO need Wayland), but it means the overlay/surface libraries are
inherently compositor-coupled.

### 2.9 phosphor-config Carries Qt6::Gui

phosphor-config links Qt6::Gui publicly. A configuration library should not need
Gui. This may be due to QColor in config values or similar. Worth investigating
whether the Gui dependency can be moved to PRIVATE or removed entirely.

---

## 3. Target Library Stack

### 3.1 Design Principles

1. **Strict layering**: primitives -> domain -> contracts -> engines -> daemon
2. **No upward dependencies**: domain models never depend on engine contracts
3. **Minimal engine SDK**: a new engine links at most 2-3 libraries
4. **Consumer-profile optimization**: each consumer links only what it needs
5. **No circular dependencies**: verified by the DAG constraint

### 3.2 Target Library Hierarchy

```
TIER 0 — Foundational Primitives (zero phosphor deps, Qt::Core only)
├── phosphor-identity        (INTERFACE, unchanged)
├── phosphor-config          (SHARED, unchanged except remove Qt::Gui if possible)
├── phosphor-protocol        (SHARED, unchanged)
├── phosphor-audio           (SHARED, unchanged)
└── phosphor-layout-api      (SHARED, unchanged)

TIER 1 — Domain Models (depend on Tier 0 only)
├── phosphor-zones-core      [NEW: split from phosphor-zones]
│   Zone, Layout, ZoneDetector, ZoneHighlighter, LayoutUtils,
│   LayoutRegistry, ZonesLayoutSource, ZonesLayoutSourceFactory,
│   AssignmentEntry, ZoneDefaults, ZoneJsonKeys, IZoneDetector,
│   IZoneLayoutRegistry
│   PUBLIC deps: layout-api, config
│   PRIVATE deps: identity, screens-core
│
├── phosphor-tiles-core      [RENAME: phosphor-tiles stays as-is conceptually]
│   TilingAlgorithm, AlgorithmRegistry, SplitTree, TilingState,
│   ScriptedAlgorithm*, AutotileLayoutSource, AutotilePreviewRender
│   PUBLIC deps: layout-api
│   (no engine-api dependency)
│
├── phosphor-screens         (SHARED, unchanged)
│   PUBLIC deps: identity
│   PRIVATE deps: shell
│
└── phosphor-animation       (SHARED, unchanged)

TIER 2 — Engine Contracts (the "SDK" layer)
├── phosphor-engine-types    [NEW: split from phosphor-engine-api]
│   NavigationContext, WindowState enum
│   INTERFACE library (header-only, like identity)
│   PUBLIC deps: Qt6::Core only
│
├── phosphor-engine-api      [SLIMMED: only the engine interface]
│   IPlacementEngine, IPlacementState, PlacementEngineBase
│   PUBLIC deps: engine-types
│   (NO IWindowTrackingService, NO IGeometrySettings,
│    NO GeometryUtils, NO EngineTypes grab-bag)
│
└── phosphor-geometry        [NEW: split from engine-api + zones]
    GeometryUtils (the stateless math: snapToRect, coordinate transforms,
    overlap removal, min-size enforcement)
    PUBLIC deps: Qt6::Core, Qt6::Gui
    (zero phosphor deps -- pure geometry math)

TIER 3 — Engine Implementations (depend on Tier 2 + Tier 1)
├── phosphor-snap-engine     [NEW: extracted from src/snap/]
│   SnapEngine, SnapState, snap-specific GeometryUtils
│   PUBLIC deps: engine-api, zones-core, geometry
│   Consumed by: daemon
│
└── phosphor-tile-engine     [NEW: extracted from src/autotile/]
    AutotileEngine, AutotileConfig, SettingsBridge, etc.
    PUBLIC deps: engine-api, tiles-core, geometry
    Consumed by: daemon

TIER 4 — Compositor Integration (Wayland/GPU, heavy deps)
├── phosphor-shell           (SHARED, unchanged)
├── phosphor-rendering       (SHARED, unchanged, deps: shell)
├── phosphor-layer           (SHARED, unchanged, deps: shell)
├── phosphor-surfaces        (SHARED, unchanged, deps: layer)
└── phosphor-shortcuts       (SHARED, unchanged)

TIER 5 — Application (GPL)
├── plasmazones_compositor_common  (STATIC, deps: protocol, identity, animation)
├── plasmazones_core               (SHARED, deps: everything -- but engines via factory)
├── plasmazones_rendering          (SHARED, deps: core, rendering)
├── plasmazonesd                   (daemon executable)
├── plasmazones-editor             (editor executable)
└── plasmazones-settings           (settings executable)
```

### 3.3 What Changes for Each Existing Library

| Current Library | Action | Rationale |
|----------------|--------|-----------|
| phosphor-identity | **Keep as-is** | Perfect: INTERFACE, zero deps, stable |
| phosphor-config | **Keep as-is** (investigate Qt::Gui) | Solid, may be able to drop Gui |
| phosphor-layout-api | **Keep as-is** | Clean contract, stable |
| phosphor-engine-api | **Slim down dramatically** | Remove IWindowTrackingService, IGeometrySettings, GeometryUtils, EngineTypes.h; keep only IPlacementEngine, IPlacementState, PlacementEngineBase |
| phosphor-protocol | **Keep as-is** | Clean D-Bus wire types |
| phosphor-animation | **Keep as-is** | Self-contained, well-designed |
| phosphor-audio | **Keep as-is** | Self-contained |
| phosphor-shortcuts | **Keep as-is** | Self-contained |
| phosphor-shell | **Keep as-is** | Correctly encapsulates Wayland |
| phosphor-rendering | **Keep as-is** | Correctly depends on shell |
| phosphor-layer | **Keep as-is** | Correctly depends on shell |
| phosphor-surfaces | **Keep as-is** | Correctly depends on layer |
| phosphor-screens | **Keep as-is** | Clean, good interface segregation |
| phosphor-tiles | **Remove engine-api dep** | TilingState::IPlacementState impl moves to tile-engine |
| phosphor-zones | **Split: remove SnapState, remove engine-api dep** | SnapState moves to snap-engine; GeometryUtils bridge moves out |

New libraries to create:

| New Library | Contents | Type |
|-------------|----------|------|
| phosphor-engine-types | NavigationContext, WindowState enum, value types needed by engines | INTERFACE |
| phosphor-geometry | Stateless geometry math (snapToRect, coordinate transforms, overlap removal) | SHARED |
| phosphor-snap-engine | SnapEngine + SnapState + snap-specific files from src/snap/ | SHARED |
| phosphor-tile-engine | AutotileEngine + supporting files from src/autotile/ | SHARED |

### 3.4 Key Dependency Changes

**Before (problematic):**
```
zones --PUBLIC--> engine-api    (domain depends on engine contract)
tiles --PUBLIC--> engine-api    (domain depends on engine contract)
```

**After (clean layering):**
```
snap-engine --PUBLIC--> engine-api, zones    (engine depends on domain + contract)
tile-engine --PUBLIC--> engine-api, tiles    (engine depends on domain + contract)
zones ----/---> engine-api                   (no dependency)
tiles ----/---> engine-api                   (no dependency)
```

---

## 4. Engine SDK Surface

### 4.1 What an Engine Developer Links

To build a new placement engine (e.g., a tabbed/stacking engine), a developer links:

```cmake
target_link_libraries(MyEngine
    PUBLIC
        PhosphorEngineApi::PhosphorEngineApi   # IPlacementEngine, PlacementEngineBase
        PhosphorGeometry::PhosphorGeometry     # snapToRect, overlap removal (optional)
)
```

That is **two libraries**, bringing:
- Qt6::Core (from engine-api)
- Qt6::Gui (from geometry, if used)
- Zero Wayland, zero D-Bus, zero KDE dependencies

### 4.2 Headers the Engine Developer Includes

**From phosphor-engine-api (mandatory):**

| Header | Purpose |
|--------|---------|
| `IPlacementEngine.h` | The interface to implement |
| `IPlacementState.h` | Per-screen state contract to implement |
| `PlacementEngineBase.h` | Optional base class (window state FSM, unmanaged geometry) |
| `NavigationContext.h` | Input struct for navigation methods (windowId + screenId) |

**From phosphor-engine-types (transitive via engine-api):**

| Header | Purpose |
|--------|---------|
| `WindowState.h` | Unmanaged/EngineOwned/Floated enum |
| `NavigationContext.h` | (Same as above, may live here instead) |

**From phosphor-geometry (optional):**

| Header | Purpose |
|--------|---------|
| `GeometryUtils.h` | snapToRect, coordinate transforms, overlap removal |

### 4.3 What the Engine Implements

```cpp
class MyEngine : public PhosphorEngineApi::PlacementEngineBase
{
    Q_OBJECT
public:
    // === MANDATORY (pure virtual in IPlacementEngine) ===
    bool isActiveOnScreen(const QString& screenId) const override;
    void windowOpened(const QString& windowId, const QString& screenId,
                      int minWidth, int minHeight) override;
    void windowClosed(const QString& windowId) override;
    void windowFocused(const QString& windowId, const QString& screenId) override;
    void toggleWindowFloat(const QString& windowId, const QString& screenId) override;
    void setWindowFloat(const QString& windowId, bool shouldFloat) override;
    void focusInDirection(const QString& direction, const NavigationContext& ctx) override;
    void moveFocusedInDirection(const QString& direction, const NavigationContext& ctx) override;
    void swapFocusedInDirection(const QString& direction, const NavigationContext& ctx) override;
    void moveFocusedToPosition(int position, const NavigationContext& ctx) override;
    void rotateWindows(bool clockwise, const NavigationContext& ctx) override;
    void reapplyLayout(const NavigationContext& ctx) override;
    void snapAllWindows(const NavigationContext& ctx) override;
    void cycleFocus(bool forward, const NavigationContext& ctx) override;
    void pushToEmptyZone(const NavigationContext& ctx) override;
    void restoreFocusedWindow(const NavigationContext& ctx) override;
    void toggleFocusedFloat(const NavigationContext& ctx) override;
    void saveState() override;
    void loadState() override;
    IPlacementState* stateForScreen(const QString& screenId) override;
    const IPlacementState* stateForScreen(const QString& screenId) const override;

    // === MANDATORY (pure virtual in PlacementEngineBase) ===
    void onWindowClaimed(const QString& windowId) override;
    void onWindowReleased(const QString& windowId) override;
    void onWindowFloated(const QString& windowId) override;
    void onWindowUnfloated(const QString& windowId) override;
};
```

### 4.4 What the Engine Consumes (Injected by the Daemon)

The daemon injects runtime services via IPlacementEngine methods:

| Method | What it provides | Engine's use |
|--------|-----------------|--------------|
| `syncFromSettings(QObject*)` | The daemon's Settings object | Engine qobject_casts to extract tuning values |
| `connectToSettings(QObject*)` | Same, for live-update signals | Engine connects signals for dynamic reconfiguration |
| `setWindowRegistry(QObject*)` | Window-class metadata | App-rule matching |
| `setIsWindowFloatingFn(fn)` | Global float predicate | Cross-engine float coordination |

These are opaque QObject pointers -- the engine does not need to link the
daemon's Settings class. The engine-api header carries no reference to the
daemon's types.

### 4.5 Signal Contracts

Engines emit these signals (inherited from PlacementEngineBase):

| Signal | When |
|--------|------|
| `geometryRestoreRequested(windowId, geometry, screenId)` | Engine wants to restore a window's pre-snap geometry |
| `windowStateTransitioned(windowId, oldState, newState)` | Window moved between Unmanaged/EngineOwned/Floated |
| `navigationFeedback(success, action, reason, source, target, screen)` | Result of a navigation operation |
| `windowFloatingChanged(windowId, floating, screenId)` | Float state changed |
| `activateWindowRequested(windowId)` | Engine wants to activate (focus) a window |
| `windowFloatingStateSynced(windowId, floating, screenId)` | Passive float-state sync |
| `windowsBatchFloated(windowIds, screenId)` | Overflow windows batch-floated |
| `algorithmChanged(algorithmId)` | Active algorithm changed |
| `placementChanged(screenId)` | Layout/placement changed for a screen |
| `windowsReleased(windowIds, releasedScreenIds)` | Windows released from engine management |

### 4.6 Engine Lifecycle

1. Engine is constructed by the daemon's `EngineFactory`
2. `connectToSettings()` and `syncFromSettings()` are called
3. `setWindowRegistry()` and `setIsWindowFloatingFn()` are called
4. `setActiveScreens()` is called with the initial screen set
5. Window lifecycle events flow in: `windowOpened`, `windowClosed`, `windowFocused`
6. Navigation intents flow in: `focusInDirection`, `moveFocusedInDirection`, etc.
7. `saveState()` / `loadState()` for session persistence
8. Engine is destroyed when mode changes or daemon shuts down

---

## 5. Migration Roadmap

### 5.0 Guiding Principles

- Each PR must leave the build green
- Prefer mechanical moves (file moves + CMake rewiring) over interface redesigns
- Do not change runtime behavior -- pure structural refactoring
- PRs that only move files can be parallelized
- PRs that change interface signatures must be sequential

**NOTE**: Step numbers below (8–15) are logical sequence numbers in the
migration plan, not GitHub PR numbers. Map to actual PRs as work is merged.

### Phase A: Prepare the Ground (2 PRs, sequential)

#### PR 8: Extract phosphor-geometry from phosphor-engine-api

**Goal**: Move stateless geometry math out of engine-api into its own library.

**What moves**:
- `PhosphorEngineApi::GeometryUtils` (GeometryUtils.h, GeometryUtils.cpp) -> `PhosphorGeometry::GeometryUtils`
- `PhosphorEngineApi::JsonKeys` (JsonKeys.h) -> `PhosphorGeometry::JsonKeys`
  (these are geometry JSON serialization keys, not engine-specific)

**What stays in engine-api**:
- IPlacementEngine, IPlacementState, PlacementEngineBase, NavigationContext

**CMake changes**:
- New `libs/phosphor-geometry/CMakeLists.txt` (SHARED, deps: Qt6::Core, Qt6::Gui)
- engine-api: remove GeometryUtils.cpp from sources, add PUBLIC dep on phosphor-geometry
  (temporary, to avoid breaking downstream in one step)
- phosphor-zones: add PUBLIC dep on phosphor-geometry (it already uses these functions)

**Downstream impact**: None. The `using` declarations in `PhosphorZones::GeometryUtils`
continue to work because engine-api still re-exports geometry transitively. The goal
is to make the library exist; severing the transitive link comes in a later PR.

**Risk**: Low. Purely mechanical file move + CMake additions.

#### PR 9: Extract phosphor-engine-types from phosphor-engine-api

**Goal**: Move value types and service interfaces out of engine-api.

**What moves out of engine-api**:
- `EngineTypes.h` (TilingStateKey, SnapIntent, ResnapEntry, PendingRestore,
  SnapResult, UnfloatResult, ZoneAssignmentEntry) -> stays in engine-api for now
  but gets a clear TODO comment that snap-specific types will move to snap-engine
  and tile-specific types will move to tile-engine in Phase C
- `IWindowTrackingService.h` -> stays in engine-api (moved in Phase C when
  engines are extracted, since the daemon still needs it)
- `IVirtualDesktopManager.h` -> stays in engine-api (tiny, stable)
- `IGeometrySettings.h` + `PerScreenKeys.h` -> stays (used by both engines)

**Rationale for deferral**: Moving IWindowTrackingService and IGeometrySettings
requires the engines to be extracted first, because they are the primary consumers.
Moving them before the engines exist as separate libraries would just shuffle code
without reducing coupling.

**What actually happens in this PR**:
- Add `NavigationContext.h` and `WindowState` enum documentation
- Add `PHOSPHOR_ENGINE_API_DEPRECATED` markers on IWindowTrackingService,
  signaling that it will move when engines are extracted
- Clean up EngineTypes.h: clearly document which types are snap-specific vs
  tile-specific vs universal

**Risk**: Very low. Documentation + annotation only.

### Phase B: Break the Upward Dependency (2 PRs, sequential)

#### PR 10: Remove engine-api dependency from phosphor-zones

**Goal**: phosphor-zones should not depend on phosphor-engine-api.

**What changes**:
- **SnapState** moves from phosphor-zones to a new location. Two options:
  - Option A: Create `libs/phosphor-snap-engine/` now and put SnapState there
    (but SnapEngine itself stays in src/ until Phase C -- incomplete extraction)
  - Option B: Move SnapState into `src/snap/` in the daemon, alongside SnapEngine.
    It will move to phosphor-snap-engine in Phase C.
  - **Recommended: Option B**. SnapState is engine-runtime state that belongs
    with its engine. Moving it to the daemon first is honest about its current
    coupling. Creating a half-empty phosphor-snap-engine library just to hold
    SnapState would be premature.

- **GeometryUtils bridge**: `PhosphorZones::GeometryUtils` currently does two things:
  1. Re-exports `PhosphorEngineApi::GeometryUtils::*` via `using` declarations
  2. Adds zone-specific functions (getZoneGeometryWithGaps, etc.)

  After PR 8 created phosphor-geometry, the `using` declarations should point at
  `PhosphorGeometry::GeometryUtils::*` instead. Then phosphor-zones drops its
  engine-api dependency and takes a dependency on phosphor-geometry instead.

  The zone-specific functions that take `IGeometrySettings*` parameters remain in
  phosphor-zones but the parameter type changes: either IGeometrySettings moves to
  a lower-tier library (phosphor-config? phosphor-geometry?) or the zone geometry
  functions accept concrete gap values instead of an interface pointer.

  **Recommended**: Make `getEffectiveZonePadding` and `getEffectiveOuterGaps` accept
  a simple `struct GapConfig { int innerGap; EdgeGaps outerGaps; }` instead of
  `IGeometrySettings*`. The daemon resolves settings -> GapConfig at the call site.
  This completely decouples zone geometry calculations from the settings interface.

**CMake changes**:
- phosphor-zones: remove PUBLIC dep on PhosphorEngineApi; add PUBLIC dep on
  PhosphorGeometry; remove SnapState sources
- SnapState.h/cpp move to src/snap/

**Risk**: Medium. SnapState removal changes phosphor-zones' public API.
Callers that include `<PhosphorZones/SnapState.h>` need path updates.
All such callers are in the daemon (src/), so this is localized.

#### PR 11: Remove engine-api dependency from phosphor-tiles

**Goal**: phosphor-tiles should not depend on phosphor-engine-api.

**What changes**:
- TilingState's IPlacementState implementation: same pattern as SnapState.
  If TilingState implements IPlacementState, it needs engine-api. Move the
  IPlacementState implementation to an adapter in the daemon or in the future
  tile-engine library.
- Verify whether phosphor-tiles actually uses anything else from engine-api.
  If it's only TilingState implementing IPlacementState, the fix is either:
  1. Move TilingState to the daemon (like SnapState in PR 10)
  2. Or, define IPlacementState in a lower-tier library

  **Recommended**: TilingState is purely a tiling data structure (window order,
  split ratios, master count). The IPlacementState methods on it are a thin
  adapter. Keep TilingState in phosphor-tiles as a pure data class, and
  create a `TilingPlacementStateAdapter` in the daemon (or tile-engine) that
  wraps TilingState and implements IPlacementState.

**CMake changes**:
- phosphor-tiles: remove PUBLIC dep on PhosphorEngineApi
- Add adapter code in daemon

**Risk**: Medium. Same pattern as PR 10.

### Phase C: Extract Engines (2 PRs, parallelizable)

These two PRs are independent and can be developed in parallel.

#### PR 12: Create phosphor-snap-engine

**Goal**: Move SnapEngine + SnapState + supporting files out of the daemon.

**What moves**:
- `src/snap/SnapEngine.{h,cpp}` + all `src/snap/snapengine/*.cpp`
- `src/snap/SnapState.{h,cpp}` (moved from phosphor-zones in PR 10 to src/,
  now moves to its permanent home)

**New library**: `libs/phosphor-snap-engine/`
```cmake
add_library(PhosphorSnapEngine SHARED ...)
target_link_libraries(PhosphorSnapEngine
    PUBLIC
        PhosphorEngineApi::PhosphorEngineApi
        PhosphorZones::PhosphorZones  # Zone, Layout for zone geometry
        PhosphorGeometry::PhosphorGeometry
    PRIVATE
        PhosphorIdentity::PhosphorIdentity
        PhosphorScreens::PhosphorScreens
)
```

**What stays in daemon**: EngineFactory (creates SnapEngine), D-Bus adaptors
(SnapAdaptor), WindowTrackingService (the daemon's IWindowTrackingService impl).

**Key interface change**: SnapEngine currently calls methods on the daemon's
WindowTrackingService directly. After extraction, it should receive an
IWindowTrackingService* via constructor injection. IWindowTrackingService stays
in engine-api for now (it will be cleaned up in Phase D).

**Risk**: Medium-high. SnapEngine has the most coupling to daemon internals
(WindowTrackingService, Settings, ScreenManager). Each coupling point needs
to become constructor-injected.

#### PR 13: Create phosphor-tile-engine

**Goal**: Move AutotileEngine + supporting files out of the daemon.

**What moves**:
- `src/autotile/AutotileEngine.{h,cpp}`
- `src/autotile/AutotileConfig.{h,cpp}`
- `src/autotile/PerScreenConfigResolver.{h,cpp}`
- `src/autotile/NavigationController.{h,cpp}`
- `src/autotile/SettingsBridge.{h,cpp}`
- `src/autotile/OverflowManager.{h,cpp}`

**New library**: `libs/phosphor-tile-engine/`
```cmake
add_library(PhosphorTileEngine SHARED ...)
target_link_libraries(PhosphorTileEngine
    PUBLIC
        PhosphorEngineApi::PhosphorEngineApi
        PhosphorTiles::PhosphorTiles
        PhosphorGeometry::PhosphorGeometry
    PRIVATE
        PhosphorScreens::PhosphorScreens
)
```

**Risk**: Medium. AutotileEngine has somewhat less coupling than SnapEngine
because it was designed later with more interface awareness.

### Phase D: Clean Up the Engine API (2 PRs, sequential)

#### PR 14: Slim IWindowTrackingService

**Goal**: IWindowTrackingService is a snap-mode-specific service. Now that
snap-engine exists as a separate library, IWindowTrackingService should either:
1. Move to phosphor-snap-engine (it's only consumed by SnapEngine)
2. Or be split: universal parts stay in engine-api, snap-specific parts move out

**Recommended**: Audit IWindowTrackingService. If AutotileEngine does not
consume it at all, move it entirely to phosphor-snap-engine. If both engines
need parts of it, split it into:
- `IWindowTracker` (universal: window focus, screen resolution) in engine-api
- `IZoneAssignmentService` (snap-specific: zone assignments, resnap) in snap-engine

Remove the `PhosphorZones::Layout*` forward declaration from the universal interface.

**Risk**: Medium. Interface change affects both engine implementations.

#### PR 15: Move snap-specific EngineTypes to phosphor-snap-engine

**Goal**: EngineTypes.h in engine-api currently carries types used by both
engines and types used by only one. Split them:

**Stays in engine-api** (universal):
- TilingStateKey (used by both engines for desktop/activity keying)
- WindowState enum (already in PlacementEngineBase)

**Moves to phosphor-snap-engine**:
- SnapIntent, SnapResult, UnfloatResult, ZoneAssignmentEntry, PendingRestore,
  ResnapEntry

**Moves to phosphor-tile-engine**:
- (Any tile-specific types, if they exist)

**Risk**: Low-medium. Mechanical move + include path updates.

### Phase E: Future Optimization (optional, low priority)

#### PR 16+: Remove Qt::Gui from phosphor-config

Investigate whether phosphor-config's Qt6::Gui dependency can be dropped.
If QColor is stored in config values, consider serializing as QString
and converting at the caller.

#### PR 17+: phosphor-engine-plugin (dynamic engine loading)

Once engines are in separate libraries, add an `IEnginePlugin` interface
and a plugin loader to the daemon, so engines can be loaded as shared
libraries at runtime without recompiling the daemon. This enables true
third-party engine development.

---

## 6. Consumer Profiles After Migration

### 6.1 Engine Developer (e.g., tabbed-engine)

```cmake
find_package(PhosphorEngineApi REQUIRED)  # IPlacementEngine, PlacementEngineBase
find_package(PhosphorGeometry)            # Optional: geometry utilities

target_link_libraries(MyTabbedEngine
    PUBLIC PhosphorEngineApi::PhosphorEngineApi
    PRIVATE PhosphorGeometry::PhosphorGeometry
)
```

**Link closure**: Qt6::Core, Qt6::Gui, PhosphorEngineApi, PhosphorGeometry
**Count**: 2 phosphor libraries + 2 Qt modules

### 6.2 Compositor Plugin (KWin effect, Wayfire plugin)

```cmake
target_link_libraries(MyCompositorPlugin PRIVATE
    PhosphorProtocol::PhosphorProtocol    # D-Bus wire types
    PhosphorIdentity::PhosphorIdentity    # Window/screen ID parsing
    PhosphorAnimation::PhosphorAnimation  # Window animation
)
```

**Link closure**: Qt6::Core, Qt6::Gui, Qt6::DBus, 3 phosphor libs
**Count**: 3 phosphor libraries (same as today -- compositor-common wraps these)

### 6.3 Settings UI

```cmake
target_link_libraries(MySettingsApp PRIVATE
    PhosphorLayoutApi::PhosphorLayoutApi  # Layout previews
    PhosphorZones::PhosphorZones          # Zone layouts
    PhosphorTiles::PhosphorTiles          # Tiling algorithm previews
    PhosphorConfig::PhosphorConfig        # Configuration read/write
    PhosphorProtocol::PhosphorProtocol    # D-Bus communication with daemon
)
```

**Link closure**: Qt6::Core, Qt6::Gui, Qt6::Qml, Qt6::DBus, 5 phosphor libs
**Count**: 5 phosphor libraries (does NOT need engine-api, engines, shell, etc.)

### 6.4 Effect/Overlay (Zone rendering + drag snapping)

```cmake
target_link_libraries(MyOverlay PRIVATE
    PhosphorLayer::PhosphorLayer          # Layer-shell surface management
    PhosphorRendering::PhosphorRendering  # GPU shader rendering
    PhosphorZones::PhosphorZones          # Zone geometry for overlay positioning
    PhosphorAnimation::PhosphorAnimation  # Snap/show/hide animations
    PhosphorSurfaces::PhosphorSurfaces    # Managed surface lifecycle
)
```

**Link closure**: Qt6::Core, Qt6::Gui, Qt6::Quick, Qt6::Qml + Wayland deps + 5 phosphor libs
**Count**: 5 phosphor libraries (Wayland dependencies are inherent for overlays)

---

## 7. Summary: PR Sequence

| PR | Name | Deps | Risk | Parallelizable With |
|----|------|------|------|---------------------|
| **8** | Extract phosphor-geometry | -- | Low | -- |
| **9** | Annotate engine-api for future splits | 8 | Very Low | -- |
| **10** | Remove engine-api dep from phosphor-zones | 8 | Medium | -- |
| **11** | Remove engine-api dep from phosphor-tiles | 8 | Medium | 10 |
| **12** | Create phosphor-snap-engine | 10 | Medium-High | 13 |
| **13** | Create phosphor-tile-engine | 11 | Medium | 12 |
| **14** | Slim IWindowTrackingService | 12, 13 | Medium | -- |
| **15** | Move snap-specific EngineTypes | 14 | Low-Medium | -- |

**Critical path**: 8 -> 10 -> 12 -> 14 -> 15
**Parallel track**: 8 -> 11 -> 13 (merges at 14)

**Estimated scope**: 8 PRs, approximately 4-5 weeks with the parallel tracks.

---

## 8. Validation Checklist

After the full migration, verify:

- [ ] `phosphor-zones` has zero dependency on `phosphor-engine-api`
- [ ] `phosphor-tiles` has zero dependency on `phosphor-engine-api`
- [ ] `phosphor-engine-api` contains only: IPlacementEngine, IPlacementState,
      PlacementEngineBase, NavigationContext, WindowState
- [ ] A new engine can be built linking only phosphor-engine-api + phosphor-geometry
- [ ] `IWindowTrackingService` does not forward-declare `PhosphorZones::Layout*`
- [ ] All tests pass in Docker (`docker run --rm -v "$PWD":/src plasmazones-build ctest --output-on-failure`)
- [ ] No circular dependencies in the CMake graph
- [ ] Each library can be built standalone (find_package fallback paths work)
- [ ] EngineFactory in the daemon can still create both engines via constructor injection
