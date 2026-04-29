# Library Architecture Audit

Date: 2026-04-24
Branch: feat/snap-state-ownership
Auditor: Architecture Agent (Opus 4.6)
Scope: 19 phosphor-* libraries under libs/

---

## 1. Dependency Graph

### 1.1 Complete Dependency Map (PUBLIC deps only)

```
Legend:  A --> B   means A has PUBLIC link to B
        A ..> B   means A has PRIVATE link to B

Leaf libraries (no phosphor deps):
  PhosphorGeometry       --> Qt6::Core
  PhosphorIdentity       --> Qt6::Core                  (INTERFACE)
  PhosphorEngineTypes    --> Qt6::Core                  (INTERFACE)
  PhosphorLayoutApi      --> Qt6::Core
  PhosphorConfig         --> Qt6::Core, Qt6::Gui
  PhosphorProtocol       --> Qt6::Core, Qt6::DBus
  PhosphorAnimation      --> Qt6::Core, Qt6::Gui
  PhosphorAudio          --> Qt6::Core
  PhosphorShell          --> Qt6::Core, Qt6::Gui
  PhosphorShortcuts      --> Qt6::Core, Qt6::Gui

Mid-tier:
  PhosphorScreens        --> Qt6::Core, Qt6::Gui, Qt6::DBus, PhosphorIdentity
                         ..> PhosphorShell

  PhosphorRendering      --> Qt6::Core, Qt6::Gui, Qt6::Quick, PhosphorShell

  PhosphorLayer          --> Qt6::Core, Qt6::Gui, Qt6::Qml, Qt6::Quick, PhosphorShell

  PhosphorSurfaces       --> Qt6::Core, Qt6::Quick, PhosphorLayer

  PhosphorTiles          --> Qt6::Core, Qt6::Qml, PhosphorLayoutApi, PhosphorEngineTypes

  PhosphorZones          --> Qt6::Core, Qt6::Gui, PhosphorLayoutApi,
                             PhosphorGeometry, PhosphorConfig
                         ..> PhosphorIdentity, PhosphorScreens

  PhosphorEngineApi      --> Qt6::Core, Qt6::Gui, PhosphorEngineTypes, PhosphorGeometry

Engine libraries:
  PhosphorTileEngine     --> Qt6::Core, PhosphorEngineApi, PhosphorEngineTypes,
                             PhosphorTiles, PhosphorGeometry, PhosphorLayoutApi
                         ..> PhosphorScreens, PhosphorIdentity, PhosphorZones

  PhosphorSnapEngine     --> Qt6::Core, Qt6::Gui, PhosphorEngineApi, PhosphorEngineTypes,
                             PhosphorZones, PhosphorGeometry, PhosphorLayoutApi,
                             PhosphorProtocol
                         ..> PhosphorScreens, PhosphorIdentity, PhosphorTiles
```

### 1.2 Dependency Diagram (Text)

```
                              Qt6::Core
                             /    |    \
                            /     |     \
              PhosphorIdentity    |    PhosphorGeometry
              (INTERFACE)         |         |
                   |              |         |
              PhosphorScreens     |    PhosphorEngineTypes
                   |              |    (INTERFACE)
                   |              |       /    \
                   |    PhosphorLayoutApi  PhosphorEngineApi
                   |        |    |    \       /      \
                   |  PhosphorConfig  |   PhosphorTiles
                   |        |         |       |
                   |   PhosphorZones  |       |
                   |   /          \   |       |
                   |  /            \  |       |
            PhosphorSnapEngine   PhosphorTileEngine
```

### 1.3 Circular Dependency Check

**Result: NO circular dependencies found.**

All dependency edges flow downward through the graph. Engine libraries
depend on domain libraries, which depend on primitive/API libraries, which
depend on Qt alone. No back-edges exist.

---

## 2. Diamond Dependency Problems

### 2.1 PhosphorEngineTypes redundancy in engine libraries

**Severity: MEDIUM**

PhosphorTileEngine declares PUBLIC links to both `PhosphorEngineApi` and
`PhosphorEngineTypes`. However, PhosphorEngineApi already has a PUBLIC
link to PhosphorEngineTypes. The explicit PhosphorEngineTypes link in
PhosphorTileEngine is redundant.

Same issue exists in PhosphorSnapEngine.

Both engine libraries' CMakeLists.txt line 61/62 and 60/61 respectively:
```cmake
PhosphorEngineApi::PhosphorEngineApi
PhosphorEngineTypes::PhosphorEngineTypes   # <-- redundant
```

**ODR safety:** Since PhosphorEngineTypes is an INTERFACE (header-only)
library, there is no ODR risk from the diamond. The same headers get
included once per TU regardless. This is a cleanliness issue, not a
correctness issue.

**Fix:** Remove the explicit `PhosphorEngineTypes::PhosphorEngineTypes`
PUBLIC link from both PhosphorTileEngine and PhosphorSnapEngine. The
dependency is already satisfied transitively through PhosphorEngineApi.

### 2.2 PhosphorGeometry redundancy in engine libraries

**Severity: LOW**

Both engine libraries declare PUBLIC links to `PhosphorGeometry`, which
PhosphorEngineApi already provides transitively via its PUBLIC link to
PhosphorGeometry.

**Fix:** Remove `PhosphorGeometry::PhosphorGeometry` from engine library
PUBLIC deps. If engine headers do not directly include PhosphorGeometry
headers (they don't -- only EngineApi does), this is safe.

### 2.3 PhosphorLayoutApi redundancy in PhosphorTileEngine

**Severity: LOW**

PhosphorTileEngine links PhosphorLayoutApi PUBLIC. It also links
PhosphorTiles PUBLIC, which already links PhosphorLayoutApi PUBLIC.
Double transitive.

**Fix:** Remove explicit PhosphorLayoutApi from PhosphorTileEngine if
tile-engine headers do not directly `#include <PhosphorLayoutApi/...>`.
Checking AutotileEngine.h line 9: it includes `<PhosphorLayoutApi/EdgeGaps.h>`,
so the PUBLIC link is actually needed for header correctness. However, it
arrives transitively via PhosphorTiles. The explicit link is still
redundant. Safe to remove since transitive propagation covers it.

### 2.4 Daemon transitive deps

**Severity: LOW**

The daemon's `plasmazones_core` target links both engine libraries, plus
PhosphorZones, PhosphorTiles, PhosphorScreens, PhosphorIdentity,
PhosphorLayoutApi, PhosphorConfig, PhosphorEngineApi, and PhosphorProtocol
as direct PUBLIC deps. Most of these arrive transitively through the engine
libraries. The daemon is the final application target, so bloated link lines
are cosmetic rather than harmful, but they obscure the real dependency
intent.

**Fix:** Keep only direct dependencies the daemon's own code actually
includes. Everything already provided transitively through
PhosphorTileEngine and PhosphorSnapEngine can be dropped from the explicit
list. This is a cleanup task, not urgent.

---

## 3. Namespace Hygiene

### 3.1 PlasmaZones namespace pollution in engine libraries

**Severity: HIGH**

Both engine libraries place all their code in `namespace PlasmaZones`:

- **phosphor-tile-engine:** AutotileEngine.h, AutotileConfig.h,
  NavigationController.h, OverflowManager.h, PerScreenConfigResolver.h
  -- ALL in `namespace PlasmaZones`.

- **phosphor-snap-engine:** SnapEngine.h, snapnavigationtargets.h,
  and all .cpp files -- ALL in `namespace PlasmaZones`.

`PlasmaZones` is the daemon/application namespace. Library code should
use its own namespace. PhosphorEngineApi uses `PhosphorEngineApi::`,
PhosphorZones uses `PhosphorZones::`, PhosphorTiles uses `PhosphorTiles::`,
etc. The engines are the ONLY libraries that use `PlasmaZones`.

**Exception:** SnapState lives in `namespace PhosphorZones`, which is
correct for its domain but inconsistent with SnapEngine living in
`namespace PlasmaZones`.

**Fix:** Migrate engine code to proper library namespaces:
- PhosphorTileEngine -> `namespace PhosphorTileEngine` (or `namespace Phosphor::TileEngine`)
- PhosphorSnapEngine -> `namespace PhosphorSnapEngine` (or `namespace Phosphor::SnapEngine`)

Then add namespace aliases in the daemon for backward compatibility during
transition:
```cpp
namespace PlasmaZones {
    using PhosphorTileEngine::AutotileEngine;
    using PhosphorSnapEngine::SnapEngine;
}
```

This is a large but necessary refactor. Until completed, every consumer of
either engine library gets `PlasmaZones::` injected into their namespace
scope even though they may have nothing to do with the daemon.

### 3.2 Using-aliases in public engine headers leak to consumers

**Severity: HIGH**

SnapEngine.h (lines 30-48) contains 17 `using` declarations at namespace
scope inside `namespace PlasmaZones`:

```cpp
using NavigationContext = PhosphorEngineApi::NavigationContext;
using SnapResult = PhosphorEngineApi::SnapResult;
// ... 15 more
using PhosphorProtocol::CycleTargetResult;
using PhosphorProtocol::FocusTargetResult;
// ... etc.
```

AutotileEngine.h (lines 37-39) contains 3:
```cpp
using NavigationContext = PhosphorEngineApi::NavigationContext;
using TilingStateKey = PhosphorEngineApi::TilingStateKey;
namespace PerScreenKeys = PhosphorEngineApi::PerScreenKeys;
```

Any translation unit that includes these headers gets all these aliases
injected into `PlasmaZones::`. This is namespace pollution that makes
types ambiguous for consumers who have their own using-declarations.

**Fix:** Move the using-aliases into the class body where possible, or
into an unnamed namespace / `detail` namespace. For types used in the
public API (function signatures), use fully-qualified names instead.

---

## 4. Export Macro Usage

### 4.1 Audit Results

**Severity: NONE (clean)**

Every public class in each library uses the correct per-library export macro:

| Library | Macro | Classes checked |
|---------|-------|-----------------|
| phosphor-tile-engine | `PHOSPHORTILEENGINE_EXPORT` | AutotileEngine, AutotileConfig, NavigationController, OverflowManager, PerScreenConfigResolver |
| phosphor-snap-engine | `PHOSPHORSNAPENGINE_EXPORT` | SnapEngine, SnapState |
| phosphor-zones | `PHOSPHORZONES_EXPORT` | Zone, Layout, ZoneDetector, ZoneHighlighter, LayoutRegistry, IZoneDetector, IZoneLayoutRegistry, ZonesLayoutSource, ZonesLayoutSourceFactory |
| phosphor-engine-api | `PHOSPHORENGINEAPI_EXPORT` | PlacementEngineBase, IWindowTrackingService, IVirtualDesktopManager |
| phosphor-config | `PHOSPHORCONFIG_EXPORT` | IBackend, IGroup, JsonBackend, JsonGroup, QSettingsBackend, QSettingsGroup, Store, Schema, MigrationRunner |
| phosphor-screens | `PHOSPHORSCREENS_EXPORT` | ScreenManager, ScreenResolver, DBusScreenAdaptor, PlasmaPanelSource, NoOpPanelSource, VirtualScreenSwapper, IConfigStore, InMemoryConfigStore, IPanelSource |

No instances of `PLASMAZONES_EXPORT` found in any library code.

---

## 5. License Consistency

### 5.1 Audit Results

**Severity: NONE (clean)**

All .h and .cpp files under `libs/` use `LGPL-2.1-or-later` as their
SPDX license identifier. No GPL-3.0-or-later headers were found in any
library code. This is correct per the project's LGPL library policy.

All CMakeLists.txt files also carry `LGPL-2.1-or-later`.

---

## 6. Include Path Cleanliness

### 6.1 Daemon-relative includes in library code

**Severity: NONE (clean)**

Grep for `"core/`, `"config/`, `"dbus/`, `"daemon/` patterns in `libs/`
returned zero results. No library code includes daemon-internal headers.

### 6.2 Relative includes in library headers

**Severity: NONE (clean)**

No `"../"` relative includes found in any library header file. All
inter-library includes use angle-bracket paths
(`<PhosphorEngineApi/PlacementEngineBase.h>`, etc.).

### 6.3 PhosphorScreens in PhosphorTileEngine PUBLIC header

**Severity: MEDIUM**

AutotileEngine.h (line 28) includes:
```cpp
#include <PhosphorScreens/ScreenIdentity.h>
```

Yet PhosphorScreens is listed as a PRIVATE dependency in
PhosphorTileEngine's CMakeLists.txt (line 66). This means:

1. Consumers of PhosphorTileEngine will see `#include
   <PhosphorScreens/ScreenIdentity.h>` in the header but won't get
   PhosphorScreens on their include path unless they also link it
   themselves.
2. This works in-tree because the daemon links PhosphorScreens
   separately, but standalone consumers of PhosphorTileEngine would
   get a compile error.

**Fix:** Either:
- (a) Promote `PhosphorScreens::PhosphorScreens` to PUBLIC in
  PhosphorTileEngine's CMakeLists.txt, OR
- (b) Forward-declare the ScreenIdentity types used in AutotileEngine.h
  and move the `#include` to the .cpp file (preferred -- keeps the
  dependency graph narrower).

---

## 7. QMetaObject::invokeMethod Usage Audit

### 7.1 PhosphorTileEngine

**Location:** `AutotileEngine.cpp`

| Line | Target | Method | Assessment |
|------|--------|--------|------------|
| 1511 | `this` (AutotileEngine) | `processPendingRetiles` | **OK.** Self-invocation via QueuedConnection for event-loop deferral. Standard Qt pattern. |
| 3527 | `m_windowRegistry` | `"canonicalizeWindowId"` | **PROBLEM.** String-based dynamic invocation on an opaque QObject*. Not type-safe. |
| 3564 | `m_windowRegistry` | `"canonicalizeForLookup"` | **PROBLEM.** Same issue. |
| 3581 | `m_windowRegistry` | `"appIdFor"` | **PROBLEM.** Same issue. |

**Severity: MEDIUM**

The three `m_windowRegistry` calls use string-based method invocation
because the engine doesn't have a typed interface for the WindowRegistry.
The `setWindowRegistry(QObject*)` pattern on IPlacementEngine takes an
opaque QObject*, then the engine calls methods by name string.

**Fix:** Extract an `IWindowRegistry` interface in phosphor-engine-api:
```cpp
class IWindowRegistry {
public:
    virtual ~IWindowRegistry() = default;
    virtual QString canonicalizeWindowId(const QString& raw) const = 0;
    virtual QString canonicalizeForLookup(const QString& raw) const = 0;
    virtual QString appIdFor(const QString& windowId) const = 0;
};
```
Then change `setWindowRegistry(QObject*)` to `setWindowRegistry(IWindowRegistry*)`.
This eliminates all three string-based invokeMethod calls.

### 7.2 PhosphorSnapEngine

**Location:** `navigation_actions.cpp`, `snapnavigationtargets.cpp`

| Line | Target | Method | Assessment |
|------|--------|--------|------------|
| navigation_actions.cpp:53 | `obj` (WTA) | generic string-dispatch helper | **PROBLEM.** Calls arbitrary method by string name on the WTA back-reference. |
| navigation_actions.cpp:424 | `m_wta` | `"frameGeometry"` | **PROBLEM.** Reads frame geometry from WTA by string method. |
| snapnavigationtargets.cpp:31 | `detector` | `"getAdjacentZone"` | **PROBLEM.** Calls ZoneDetectionAdaptor by string. |
| snapnavigationtargets.cpp:40 | `detector` | `"getFirstZoneInDirection"` | **PROBLEM.** Same. |

**Severity: MEDIUM**

All four calls exist because the engine holds `QPointer<QObject>`
back-references to daemon adaptors (WindowTrackingAdaptor,
ZoneDetectionAdaptor) rather than typed interfaces.

**Fix:** Define narrow interfaces:
- `IFrameGeometrySource`: `virtual QRect frameGeometry(const QString& windowId) const = 0`
- `IZoneAdjacencyResolver`: `virtual QString getAdjacentZone(...)`, `virtual QString getFirstZoneInDirection(...)`

Wire these through constructors instead of `setWindowTrackingAdaptor(QObject*)`.

### 7.3 PhosphorLayer / PhosphorScreens

The QMetaObject::invokeMethod calls in phosphor-layer (surface.cpp,
topologycoordinator.cpp) and phosphor-screens (test code only) are all
legitimate QueuedConnection self-invocations or test helpers. No issues.

---

## 8. Settings Interface Design

### 8.1 IAutotileSettings completeness

**Severity: NONE (appears complete)**

IAutotileSettings (in phosphor-tile-engine) declares 17 virtual accessors
covering algorithm, ratios, gaps, focus, smart gaps, insert position,
overflow, sticky handling, per-algorithm settings, and max windows.

AutotileEngine accesses settings exclusively through
`qobject_cast<IAutotileSettings*>(engineSettings())`. No casts to the
daemon's concrete `Settings` class were found.

### 8.2 ISnapSettings completeness

**Severity: LOW**

ISnapSettings declares 5 virtual accessors: excludedApplications,
excludedWindowClasses, stickyWindowHandling, moveNewWindowsToLastZone,
restoreWindowsToZonesOnLogin.

SnapEngine also casts to `IGeometrySettings` (line 39 of SnapEngine.cpp):
```cpp
auto* gs = dynamic_cast<PhosphorEngineApi::IGeometrySettings*>(engineSettings());
```

IGeometrySettings is in phosphor-engine-api and provides zonePadding and
outerGaps. This is correct -- the interface lives in the right library.

**Note:** SnapEngine uses `dynamic_cast` while AutotileEngine uses
`qobject_cast`. Both work, but the inconsistency is worth standardizing.
`qobject_cast` is preferred per Qt convention (and is slightly faster
for QObject-derived types since it uses the meta-object system rather than
RTTI).

**Fix:** Change SnapEngine.cpp:34 from `dynamic_cast` to `qobject_cast`
for consistency, but only if ISnapSettings has `Q_DECLARE_INTERFACE`
(it does -- line 26 of ISnapSettings.h).

### 8.3 No concrete daemon casts

**Severity: NONE (clean)**

No `qobject_cast` or `dynamic_cast` to concrete daemon types (e.g.,
`Settings*`, `PlasmaZones::Settings*`) was found in any engine library.
All settings access goes through the defined interfaces.

---

## 9. PlacementEngineBase Bloat

### 9.1 Signal count

PlacementEngineBase declares 10 signals:

1. `geometryRestoreRequested`
2. `windowStateTransitioned`
3. `navigationFeedback`
4. `windowFloatingChanged`
5. `activateWindowRequested`
6. `windowFloatingStateSynced`
7. `windowsBatchFloated`
8. `algorithmChanged`
9. `placementChanged`
10. `settingsWriteBackRequested`

**Severity: MEDIUM**

10 signals is a lot for a base class. Several deserve scrutiny:

### 9.2 settingsWriteBackRequested -- autotile-only?

**Result: YES, autotile-only.**

`settingsWriteBackRequested` is emitted only in AutotileEngine.cpp
(lines 287, 804, 1757). SnapEngine never emits it. Zero occurrences
in phosphor-snap-engine.

**Severity: LOW**

This signal on the base class is dead weight for snap-mode. However,
moving it to AutotileEngine would require the daemon to connect to
the concrete type rather than PlacementEngineBase. Since the daemon
currently connects to this signal on both engines uniformly, the
practical impact is nil -- unused signals have zero runtime cost.

**Fix (optional):** If a third engine arrives and also doesn't need
settings write-back, consider moving this signal to a mixin or
sub-interface. For now, acceptable.

### 9.3 Signal grouping recommendation

The 10 signals fall into three logical groups:

1. **Window state** (4): geometryRestoreRequested, windowStateTransitioned,
   windowFloatingChanged, windowFloatingStateSynced
2. **Navigation** (2): navigationFeedback, activateWindowRequested
3. **Engine lifecycle** (4): windowsBatchFloated, algorithmChanged,
   placementChanged, settingsWriteBackRequested

A future refactor could split these into sub-interfaces (IWindowStateEmitter,
INavigationEmitter, IEngineLifecycleEmitter), but this is not urgent.

---

## 10. Unused Code / Dead Imports

### 10.1 Old src/autotile/ and src/snap/ directories

**Severity: NONE (clean)**

Both directories have been removed. `ls` confirms they do not exist.
The engine code lives exclusively under `libs/phosphor-tile-engine/`
and `libs/phosphor-snap-engine/`.

### 10.2 AutotileEngine.h file size

**Severity: HIGH**

- `AutotileEngine.h`: **1427 lines**
- `AutotileEngine.cpp`: **3802 lines**

Both significantly exceed the project's 800-line limit.

**Fix:** Split AutotileEngine into focused files:
- `AutotileEngine.h` -> core class declaration (target: ~400 lines)
- `AutotileEngineNavigation.h` -> navigation method declarations
- `AutotileEngineState.h` -> state serialization, pending restores
- `AutotileEngineDrag.h` -> drag-insert preview

The .cpp is already partially split (the build lists only
AutotileEngine.cpp, but the file likely contains inline'd sections).
Further splitting into per-concern .cpp files (as SnapEngine already
has: calculate.cpp, commit.cpp, float.cpp, lifecycle.cpp,
navigation.cpp, navigation_actions.cpp, resnap_calc.cpp) would bring
it into compliance.

### 10.3 Redundant forward declarations in SnapEngine.h

**Severity: LOW**

SnapEngine.h forward-declares types from PhosphorZones:
```cpp
namespace PhosphorZones {
class IZoneDetector;
class LayoutRegistry;
class SnapState;
}
```

These are appropriate since the header uses pointers/references to them.
No stale forward declarations found.

### 10.4 Empty namespace blocks in AutotileEngine.h

**Severity: LOW**

AutotileEngine.h contains an empty namespace block at lines 75-76:
```cpp
namespace PlasmaZones {
} // namespace PlasmaZones
```

This is dead code left over from a refactor.

**Fix:** Remove the empty namespace block.

---

## Summary of Issues

| # | Category | Severity | Description |
|---|----------|----------|-------------|
| 3.1 | Namespace | **HIGH** | Engine libraries use `namespace PlasmaZones` instead of their own namespace |
| 3.2 | Namespace | **HIGH** | 20 using-aliases in public engine headers pollute consumer TUs |
| 10.2 | File size | **HIGH** | AutotileEngine.h (1427 lines) and .cpp (3802 lines) exceed 800-line limit |
| 2.1 | Diamond dep | MEDIUM | PhosphorEngineTypes explicitly linked when already transitive via PhosphorEngineApi |
| 6.3 | Include path | MEDIUM | AutotileEngine.h includes PhosphorScreens header but PhosphorScreens is PRIVATE dep |
| 7.1 | invokeMethod | MEDIUM | 3 string-based invokeMethod calls for WindowRegistry in AutotileEngine |
| 7.2 | invokeMethod | MEDIUM | 4 string-based invokeMethod calls for WTA/ZoneDetector in SnapEngine |
| 9.1 | Base bloat | MEDIUM | PlacementEngineBase has 10 signals; settingsWriteBackRequested is autotile-only |
| 2.2 | Diamond dep | LOW | PhosphorGeometry explicitly linked when transitive via PhosphorEngineApi |
| 2.3 | Diamond dep | LOW | PhosphorLayoutApi explicitly linked when transitive via PhosphorTiles |
| 2.4 | Daemon deps | LOW | Daemon links libs it gets transitively through engine libraries |
| 8.2 | Settings | LOW | SnapEngine uses dynamic_cast instead of qobject_cast for ISnapSettings |
| 10.4 | Dead code | LOW | Empty namespace block in AutotileEngine.h |

### Priority-ordered fix plan

1. **Namespace migration** (3.1, 3.2) -- Highest impact on library
   reusability. Engine libraries cannot be consumed by non-daemon code
   without dragging in the PlasmaZones namespace.

2. **AutotileEngine file split** (10.2) -- Enforces project standards
   and makes the engine maintainable.

3. **PhosphorScreens include mismatch** (6.3) -- Latent build breakage
   for standalone consumers.

4. **invokeMethod cleanup** (7.1, 7.2) -- Extract IWindowRegistry and
   IZoneAdjacencyResolver interfaces to replace string-based dispatch.

5. **Redundant transitive deps** (2.1, 2.2, 2.3) -- Cosmetic cleanup
   for CMakeLists hygiene.

6. **Minor cleanups** (8.2, 9.1, 10.4, 2.4) -- Fix opportunistically.
