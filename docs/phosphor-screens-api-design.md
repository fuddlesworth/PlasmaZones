# PhosphorScreens — API Design

**Status:** initial landing complete (see "Migration sequence" for per-step status; "Deferred from initial landing" for what was explicitly left for later)
**Version:** 0.1.0 (pre-release, API unstable)
**License:** LGPL-2.1-or-later (shared library) — consumers under any GPL-compatible licence

## Goals

PhosphorScreens is a **domain-free** screen-topology library for Qt6 Wayland
applications. It owns three responsibilities that any tiling shell or
window-manager UI re-derives from scratch today:

1. **Physical screen monitoring** — wraps `QGuiApplication::screens()` with
   stable EDID-based identifiers, geometry-change fan-out, and an instant
   reactive available-geometry channel that does not poll.
2. **Virtual screen subdivision** — lets a physical monitor be carved into
   N rectangular sub-screens, each with its own stable identifier so all
   downstream per-screen state (windows, layouts, autotile state, overlays)
   keys cleanly off one ID type.
3. **Effective-screen resolution** — turns "the cursor is here" or "the
   window is there" into the right virtual-or-physical screen ID, including
   the daemon round-trip that QML callers cannot do directly.

The library is the foundation for any future PhosphorScreens-aware shell:
overlay placement, snap geometry, autotile zone math, multi-monitor
layout cycling, and KCM monitor pages all consume the same surface.

### Non-goals

- **No tiling, snapping, or layout policy.** PhosphorScreens publishes the
  geometry of every effective screen; what gets put on those screens is the
  caller's problem. This is the rule that keeps `screenmoderouter.{h,cpp}`
  out of the library — that file routes nav actions to `SnapEngine` vs
  `AutotileEngine`, which is dispatch policy, not screen management.
- **No window tracking.** PhosphorScreens never sees a `WId` or a
  `WindowIdentity`. Callers that want to move a window when its host
  virtual screen swaps regions listen to PhosphorScreens signals and run
  their own move logic.
- **No KF6 dependency in core.** The KDE Plasma panel-query backend is an
  optional CMake target so the library compiles cleanly on Hyprland,
  Sway, COSMIC, GNOME, and any other Wayland host.

## Namespace & headers

```cpp
namespace Phosphor::Screens { /* ... */ }

#include <PhosphorScreens/PhosphorScreens.h>     // umbrella
#include <PhosphorScreens/VirtualScreen.h>       // POD types (header-only)
#include <PhosphorScreens/Manager.h>             // ScreenManager
#include <PhosphorScreens/Resolver.h>            // ScreenResolver
#include <PhosphorScreens/Swapper.h>             // VirtualScreenSwapper
#include <PhosphorScreens/IPanelSource.h>        // pluggable panel backend
#include <PhosphorScreens/IConfigStore.h>        // pluggable VS config persistence
#include <PhosphorScreens/Adaptor.h>             // optional D-Bus adaptor target
```

CMake:

```cmake
find_package(PhosphorScreens 0.1 REQUIRED)
target_link_libraries(mytarget PRIVATE PhosphorScreens::PhosphorScreens)

# Optional sub-targets:
target_link_libraries(mytarget PRIVATE PhosphorScreens::PlasmaPanelSource)  # KDE Plasma panel D-Bus
target_link_libraries(mytarget PRIVATE PhosphorScreens::DBusAdaptor)        # ScreenAdaptor + interface XML
```

## Library layout

```
libs/phosphor-screens/
  CMakeLists.txt
  PhosphorScreensConfig.cmake.in
  include/PhosphorScreens/
    PhosphorScreens.h        — umbrella header
    VirtualScreen.h          — VirtualScreenDef + VirtualScreenConfig (header-only)
    Manager.h                — ScreenManager (the QObject service)
    Resolver.h               — ScreenResolver static helper
    Swapper.h                — VirtualScreenSwapper
    IPanelSource.h           — interface for panel-offset producers
    NoOpPanelSource.h        — default zero-offset implementation
    IConfigStore.h           — interface for VS config persistence
    InMemoryConfigStore.h    — default in-memory implementation
    GeometrySensor.h         — wrapper around the layer-shell sensor window
    Adaptor.h                — D-Bus adaptor (optional sub-target)
  src/
    manager.cpp
    manager_panels.cpp        — was screenmanager/panels.cpp
    manager_virtualscreens.cpp — was screenmanager/virtualscreens.cpp
    resolver.cpp
    swapper.cpp
    geometrysensor.cpp
    panelsource_plasma.cpp    — built only when PHOSPHORSCREENS_USE_PLASMA=ON
    adaptor.cpp               — built only when PHOSPHORSCREENS_BUILD_DBUS=ON
  tests/
    tst_virtualscreendef.cpp
    tst_virtualscreenconfig.cpp
    tst_swapper.cpp
    tst_manager_panels.cpp    — uses a fake IPanelSource
    tst_manager_topology.cpp  — screen add/remove/geometry signal contract
```

## Core types

### `VirtualScreenDef` / `VirtualScreenConfig` (POD, header-only)

Lift `src/core/virtualscreen.h` verbatim into `include/PhosphorScreens/VirtualScreen.h`.
Both types stay header-only (the existing impl is all inline). The validation
helper `VirtualScreenConfig::isValid()` and the swap/rotate region helpers
(`swapRegions`, `rotateRegions`) move with them — no API change.

The one rename: `PLASMAZONES_EXPORT` macro becomes `PHOSPHORSCREENS_EXPORT`.

The two POD types form the *wire format* of the library: any consumer that
serialises VS configs (Settings, JSON file, D-Bus payload) deals only in
these two structs.

### `ScreenManager` — the QObject service

```cpp
class PHOSPHORSCREENS_EXPORT ScreenManager : public QObject {
    Q_OBJECT
public:
    struct Config {
        IPanelSource*  panelSource  = nullptr;  // may be null → no panel offsets
        IConfigStore*  configStore  = nullptr;  // may be null → in-memory
        bool           useGeometrySensors = true;
        int            maxVirtualScreensPerPhysical = 8;
    };

    explicit ScreenManager(Config cfg, QObject* parent = nullptr);
    ~ScreenManager() override;

    bool init();
    void start();
    void stop();

    // ─── Physical screen queries ──────────────────────────────────────────
    QVector<QScreen*> screens() const;
    QScreen*          primaryScreen() const;
    QScreen*          screenByName(const QString& name) const;
    static QRect      actualAvailableGeometry(QScreen* screen);
    static bool       isPanelGeometryReady();
    static QScreen*   resolvePhysicalScreen(const QString& screenId);

    // ─── Virtual screen management ────────────────────────────────────────
    bool                setVirtualScreenConfig(const QString& physicalScreenId,
                                               const VirtualScreenConfig& config);
    void                refreshVirtualConfigs(const QHash<QString, VirtualScreenConfig>& configs);
    VirtualScreenConfig virtualScreenConfig(const QString& physicalScreenId) const;
    bool                hasVirtualScreens(const QString& physicalScreenId) const;

    // ─── Effective-screen queries (the unified VS-or-physical view) ───────
    QStringList effectiveScreenIds() const;
    QStringList virtualScreenIdsFor(const QString& physicalScreenId) const;
    QStringList effectiveIdsForPhysical(const QString& physicalScreenId) const;
    QRect       screenGeometry(const QString& screenId) const;
    QRect       screenAvailableGeometry(const QString& screenId) const;
    QScreen*    physicalQScreenFor(const QString& screenId) const;
    VirtualScreenDef::PhysicalEdges physicalEdgesFor(const QString& screenId) const;
    QString     virtualScreenAt(const QPoint& globalPos,
                                const QString& physicalScreenId) const;
    QString     effectiveScreenAt(const QPoint& globalPos) const;

    // ─── Panel re-query control ───────────────────────────────────────────
    void scheduleDelayedPanelRequery(int delayMs);

    // ─── Singleton accessor (kept for static helpers; see note below) ─────
    static ScreenManager* instance();
    static QStringList    effectiveScreenIdsWithFallback();

Q_SIGNALS:
    void screenAdded(QScreen* screen);
    void screenRemoved(QScreen* screen);
    void screenGeometryChanged(QScreen* screen, const QRect& geometry);
    void availableGeometryChanged(QScreen* screen, const QRect& availableGeometry);
    void panelGeometryReady();
    void delayedPanelRequeryCompleted();
    void virtualScreensChanged(const QString& physicalScreenId);
    void virtualScreenRegionsChanged(const QString& physicalScreenId);
};
```

Notes on the surface:

- The current `ScreenManager` constructor takes `QObject* parent` only and
  internally hardcodes Plasma D-Bus + Settings. The library version takes
  a small `Config` struct so the panel backend and config store are
  injectable. Daemon migration: build the same Plasma + Settings-backed
  pair and pass them in.
- The `static instance()` accessor stays. `actualAvailableGeometry`,
  `isPanelGeometryReady`, `resolvePhysicalScreen`, and
  `effectiveScreenIdsWithFallback` are all static helpers that route
  through the singleton today and must keep doing so for backwards
  compat with KCM and the layer-shell QPA plugin. The library treats
  the singleton as a convenience layer over a normal instance.
- Public API contract intent: signal semantics, panel-ready warning,
  cache invalidation rules, and the swap/rotate "all per-VS state
  follows geometry" guarantee in the existing comments stay verbatim.
- `scheduleDelayedPanelRequery(int)` is kept because external callers
  (the panel editor close path) need it. It is a panel-source operation
  in spirit — see `IPanelSource::requestRequery` below for how the
  backend hooks in.

### `IPanelSource` — pluggable panel-offset backend

```cpp
class PHOSPHORSCREENS_EXPORT IPanelSource : public QObject {
    Q_OBJECT
public:
    struct Offsets { int top=0, bottom=0, left=0, right=0; };

    /// Begin watching. Implementations may emit panelOffsetsChanged
    /// any time after this returns; the manager reads `currentOffsets`
    /// on each emission to refresh its cache.
    virtual void start() = 0;
    virtual void stop()  = 0;

    /// Snapshot for a given screen. Returns zero offsets if unknown.
    virtual Offsets currentOffsets(QScreen* screen) const = 0;

    /// Has *any* successful query landed? Drives `panelGeometryReady`.
    virtual bool ready() const = 0;

    /// Best-effort: ask the backend to re-query immediately (e.g. after
    /// the user closes a panel-editor UI). Implementations that don't
    /// support push-style refresh can no-op.
    virtual void requestRequery(int delayMs = 0) = 0;

Q_SIGNALS:
    void panelOffsetsChanged(QScreen* screen);
    void requeryCompleted();   // forwarded as delayedPanelRequeryCompleted
};
```

Backends shipped by the library:

| Backend | Class | CMake flag | Notes |
|---|---|---|---|
| No-op | `NoOpPanelSource` | always built | Always reports zeros + ready=true. Use on Wayfire / Sway / COSMIC and in tests. |
| KDE Plasma | `PlasmaPanelSource` | `PHOSPHORSCREENS_USE_PLASMA=ON` (default ON if KF6 available, but **no link to KF6** required — it's a pure D-Bus client) | Watches `org.kde.plasmashell`, queries `/PlasmaShell` for panel geometry, debounces rapid changes, fires `requestRequery` on a timer. |

The Plasma backend is implemented purely against `QtDBus` so the optional
sub-target only adds a `Qt6::DBus` link, not a KF6 dependency. This
matches the survey finding that `phosphor-screens` should have **zero KF6
headers**.

Future backends (not in 0.1):
- `WlrLayerShellPanelSource` — read panel geometry from a wlr-foreign-toplevel-aware compositor.
- `WaylandSensorOnlyPanelSource` — derive offsets purely from the geometry sensor delta. The current ScreenManager already does some of this via the sensor windows; `IPanelSource` is the place to formalise it.

### `IConfigStore` — pluggable VS config persistence

```cpp
class PHOSPHORSCREENS_EXPORT IConfigStore : public QObject {
    Q_OBJECT
public:
    virtual QHash<QString, VirtualScreenConfig> loadAll() const = 0;
    virtual bool save(const QString& physicalScreenId,
                      const VirtualScreenConfig& config) = 0;
    virtual bool remove(const QString& physicalScreenId) = 0;

Q_SIGNALS:
    /// Fires when an external writer (KCM, settings UI) modifies the store.
    /// ScreenManager listens and calls refreshVirtualConfigs().
    void changed();
};
```

This is the abstraction that fixes the survey's flagged settings-ownership
hazard:

> Settings ownership note: Virtual-screen configs live in Settings (source
> of truth); ScreenManager caches via `refreshVirtualConfigs()`.
> `VirtualScreenSwapper` mutates Settings; daemon propagates via observer.
> Two-way sync must be wired as part of extraction.

Mapping:

| Today | Library |
|---|---|
| `Settings::setVirtualScreenConfig` | `IConfigStore::save` |
| `Settings::virtualScreenConfigsChanged` | `IConfigStore::changed` |
| Daemon connects `Settings::virtualScreenConfigsChanged` → `ScreenManager::refreshVirtualConfigs` | Library does this internally when given a non-null `IConfigStore*` in `Config` |
| `VirtualScreenSwapper(Settings*)` | `VirtualScreenSwapper(IConfigStore*)` |

Built-ins:
- `InMemoryConfigStore` — for unit tests and for hosts that don't persist.
- The PlasmaZones daemon ships its own `SettingsConfigStore` adaptor that
  forwards to `PlasmaZones::Settings`. That adapter is daemon-side glue,
  not library code.

The library never reads JSON, never touches `~/.config`, and never knows
the schema-version migration rules. Persistence is entirely the host's
problem.

### `VirtualScreenSwapper`

Lift `src/core/virtualscreenswapper.{h,cpp}` with one substitution:
`Settings*` → `IConfigStore*`. The `Result` enum, `reasonString()`, and
the swap/rotate algorithms stay byte-for-byte identical.

### `ScreenResolver`

Lift `src/core/screen_resolver.{h,cpp}` verbatim. It is already a static
D-Bus client with a `QGuiApplication` fallback and zero PlasmaZones
dependencies — the `org.plasmazones.Screen` interface name becomes a
constructor parameter so non-PlasmaZones hosts can point it at their own
service:

```cpp
class PHOSPHORSCREENS_EXPORT ScreenResolver {
public:
    struct Endpoint {
        QString service   = QStringLiteral("org.plasmazones.daemon");
        QString path      = QStringLiteral("/PlasmaZones/Screen");
        QString interface = QStringLiteral("org.plasmazones.Screen");
        QString method    = QStringLiteral("getEffectiveScreenAt");
    };
    static QString effectiveScreenAt(const QPoint& pos,
                                     const Endpoint& endpoint = {},
                                     int timeoutMs = 2000);
    static QString effectiveScreenAtCursor(const Endpoint& endpoint = {},
                                           int timeoutMs = 2000);
};
```

Defaults preserve the current PlasmaZones behaviour; a future Phosphor WM
ships its own service name and overrides the endpoint.

### `GeometrySensor`

Lift the layer-shell sensor logic from `screenmanager.cpp` into a small
`GeometrySensor` class so `ScreenManager` is not directly aware of
`PhosphorLayer`:

```cpp
class PHOSPHORSCREENS_EXPORT GeometrySensor : public QObject {
    Q_OBJECT
public:
    explicit GeometrySensor(QScreen* screen, QObject* parent = nullptr);
    ~GeometrySensor() override;
    QRect availableGeometry() const;
Q_SIGNALS:
    void availableGeometryChanged(QRect geometry);
};
```

This is the only place PhosphorScreens links against `PhosphorLayer`.
The dependency is acknowledged but isolated — if a host wants to disable
layer-shell sensing entirely, set `Config::useGeometrySensors = false`
and the manager falls back to `QScreen::availableGeometry()`.

## D-Bus adaptor (optional sub-target)

`ScreenAdaptor` ships in the library but in a separate CMake target so
hosts that don't want a D-Bus surface (a pure-Qt unit-test harness, an
embedded shell, a future single-process WM) can omit it.

```
libs/phosphor-screens/dbus/
  org.phosphor.Screens.xml         — generic interface name
  adaptor.h / adaptor.cpp
```

The adaptor itself is lifted verbatim from `src/dbus/screenadaptor.{h,cpp}`
with two narrowings:

- `Q_CLASSINFO("D-Bus Interface", ...)` becomes a configurable string
  the host sets at registration time. The interface name used at the
  D-Bus session bus is also a host-side decision, so PlasmaZones keeps
  registering as `org.plasmazones.Screen` and a future Phosphor WM
  registers as `org.phosphor.Screen` against the same adaptor class.
- `Settings*` is replaced by `IConfigStore*` (matching the swapper).

The adaptor depends on `PhosphorScreens::PhosphorScreens` and on
`Qt6::DBus`. The XML interface description ships with the library so
both daemon and any external D-Bus consumer (KCM, third-party launcher)
generate the same proxy code.

## What stays in the daemon

| File | Stays | Why |
|---|---|---|
| `src/core/screenmoderouter.{h,cpp}` | ✅ daemon | Snap/autotile dispatch policy — not screen management. |
| `src/core/virtualdesktopmanager.{h,cpp}` | ✅ daemon | Virtual *desktops* (compositor concept), not virtual screens. Out of scope. |
| `src/core/screenmanager/*.cpp` glue that imports `Settings::*` directly | ✅ daemon | Concrete `IConfigStore` adapter lives daemon-side. |

The `screenmanager` static-instance pattern stays for the duration of
0.x. PlasmaZones initialises a `ScreenManager` once at daemon startup
and the static accessor returns it, exactly as today. Removing the
singleton is a 1.0 conversation, not a scaffolding-phase one.

## Settings sync contract

The two-way sync that the survey calls out is fully captured by this
sequence (no daemon-side code needed beyond the `SettingsConfigStore`
adapter):

```
[user mutates Settings]
        │
        ▼
SettingsConfigStore::save        ── persists, fires changed()
        │
        ▼
ScreenManager observes changed()
        │
        ▼
ScreenManager::refreshVirtualConfigs(store->loadAll())
        │
        ▼
emits virtualScreensChanged / virtualScreenRegionsChanged
        │
        ▼
WindowTrackingService, AutotileEngine, OverlayService react


[daemon-internal mutation: VirtualScreenSwapper::swapInDirection]
        │
        ▼
VirtualScreenConfig::swapRegions in memory
        │
        ▼
IConfigStore::save (= SettingsConfigStore → Settings)
        │
        ▼
Settings::virtualScreenConfigsChanged
        │
        ▼
SettingsConfigStore::changed (forwarded)
        │
        ▼
ScreenManager::refreshVirtualConfigs        (single source of truth path)
```

Critical: the swapper must NEVER call `ScreenManager::setVirtualScreenConfig`
directly. All writes route through the store and come back via the
observer. This is the same loop the daemon already uses — extraction
just formalises it via the interface.

The `setVirtualScreenConfig` direct call on `ScreenManager` survives
only for unit tests and for the refresh path itself (matching the
warning in `screenmanager.h`).

## Dependencies

Core:
- Qt 6.6+ (`Core`, `Gui`, `DBus`)
- `PhosphorIdentity` (for `VirtualScreenId::*` helpers — already extracted)
- `PhosphorLayer` (for the geometry sensor; optional via `Config::useGeometrySensors=false`)
- C++20

Optional sub-targets:
- `PHOSPHORSCREENS_USE_PLASMA=ON` — pulls in nothing extra; pure `Qt6::DBus` client.
- `PHOSPHORSCREENS_BUILD_DBUS=ON` — adds `Qt6::DBus` adaptor + interface XML.

**No link against any PlasmaZones target.** No link against `PhosphorConfig`,
`PhosphorZones`, `PhosphorTiles`, `PhosphorRendering`, or `KF6::*`.

## Migration sequence

Steps landed as a single PR with one commit per logical chunk. Status
annotations record what shipped vs. what was folded or deferred.

1. ✅ **Scaffold `libs/phosphor-screens/`** — empty SHARED target, umbrella
   header, `PhosphorScreensConfig.cmake.in`, top-level `add_subdirectory`.
   Landed in the same commit as step 2 because an empty SHARED target is
   awkward to ship standalone.
2. ✅ **Lift POD types** — `VirtualScreen.h` moved. `src/core/virtualscreen.h`
   is a forwarding shim that type-aliases `Phosphor::Screens::VirtualScreenDef`
   / `VirtualScreenConfig` back into `PlasmaZones::` for the ~30 existing
   in-tree call sites.
3. ✅ **Lift `ScreenResolver`** — `Endpoint` struct (renamed
   `ResolverEndpoint`, hoisted to namespace scope so its in-class member
   initialisers reach the default-arg site) + `Phosphor::Screens::ScreenResolver`
   class. `src/core/screen_resolver.h` shim dropped in a follow-up commit;
   `src/editor/main.cpp` (the only in-tree caller) uses the canonical path.
4. ✅ **Pluggable backend contracts** — `IPanelSource` + `NoOpPanelSource`
   + `PlasmaPanelSource` all landed. `PlasmaPanelSource` is pure Qt6::DBus
   against `org.kde.plasmashell`; no KF6 link, confirming the lib's
   "no KF6" invariant.
5. ✅ **Persistence contracts** — `IConfigStore` + `InMemoryConfigStore`
   in the lib. `SettingsConfigStore` adapter daemon-side in
   `src/config/settingsconfigstore.{h,cpp}`. Interface grew a single-key
   `get(physicalScreenId)` overload alongside `loadAll` so the swapper
   doesn't round-trip through the full map for one-monitor mutations.
6. ✅ **Move `VirtualScreenSwapper`** — constructor takes `IConfigStore*`.
   Both daemon and `ScreenAdaptor` own a `SettingsConfigStore` member
   ahead of the swapper for destruction order. All 19 swapper tests pass.
7. ✅ **Move `ScreenManager` core** — `ScreenManagerConfig` struct (top-level,
   not nested — same reason as `ResolverEndpoint`) carries the injectable
   `IPanelSource*` / `IConfigStore*` / geometry-sensor toggle /
   VS-cap knob. Defaulted to `ScreenManagerConfig{}` so tests and any
   future harness can `ScreenManager()` with no args.

   **Scope expansion:** eliminated the singleton + statics from the lib
   entirely (see "Singleton scope" under "Resolved during implementation").
   Daemon ships a thin service-locator
   (`src/core/screenmanagerservice.{h,cpp}`) providing `screenManager()`
   plus `actualAvailableGeometry` / `isPanelGeometryReady` /
   `resolvePhysicalScreen` / `effectiveScreenIdsWithFallback` free
   functions that delegate to the registered instance with Qt-only
   fallbacks for cold-start and tests. 107 `ScreenManager::instance()` /
   `ScreenManager::<static>` call sites across 39 files mass-renamed by
   `sed`. Five forward-decl headers got proper qualified forward
   declarations + using-alias.
8. ✅ **Move `panels.cpp` / `virtualscreens.cpp`** — folded into step 7.
   `panels.cpp` didn't reappear under `manager_panels.cpp` because its
   D-Bus query code moved into `PlasmaPanelSource`; the remaining
   "merge sensor + panel-offsets into an available rect" logic lives
   inline in `manager.cpp`'s `calculateAvailableGeometry`.
9. 🟡 **Move `ScreenAdaptor`** — **deferred.** See "Deferred from
   initial landing" below. `ScreenAdaptor` continues to live in
   `src/dbus/` until a concrete non-PlasmaZones consumer shows up.
10. 🟡 **Drop the forwarding shims** — **partial.**
    `src/core/screen_resolver.h` deleted; the rest intentionally remain.
    See "Deferred from initial landing" for the list and the reasoning.

Concurrent with the main sequence:

- ✅ **`ScreenIdentity` helpers moved to the library.** The
  `Utils::screenIdentifier` / `findScreenByIdOrName` / `screensMatch` /
  family that previously lived in `src/core/utils.cpp` are now in
  `Phosphor::Screens::ScreenIdentity`. The daemon's `Utils::*` functions
  are thin trampolines into the lib; daemon-side API surface unchanged.
- ✅ **EDID primitives moved to `phosphor-identity`.** The
  `ScreenIdUtils::readEdidHeaderSerial` / `buildScreenBaseId` /
  `normalizeHexSerial` / `invalidateEdidCache` family that previously
  lived in `src/compositor-common/screen_id.{h,cpp}` is now
  `PhosphorIdentity::ScreenId` — header-only inline (C++17 inline
  function-static guarantees a single cache across TUs, so PhosphorIdentity
  stays INTERFACE with no SHARED conversion). The KWin effect still
  reaches the helpers via `compositor-common/screen_id.h` which is now
  a namespace-alias shim.
- ✅ **Pre-existing `easingcurve.cpp` missing-include bug fixed.**
  Only surfaced with unity builds disabled.

## Deferred from initial landing

These items are named in the design but intentionally stayed behind
when the main PR landed. Each entry records what, why, and what it
would take to land later.

### 🟡 `ScreenAdaptor` lift

**What the design says.** Move `src/dbus/screenadaptor.{h,cpp}` into
an optional `PhosphorScreens::DBusAdaptor` sub-target. Parameterise
`Q_CLASSINFO("D-Bus Interface", ...)` so a future Phosphor WM can
register the same adaptor class against `org.phosphor.Screen` while
PlasmaZones keeps registering against `org.plasmazones.Screen`.

**Why deferred.** `Q_CLASSINFO` is compile-time only. Parameterising
the interface name requires one of:

1. An inheritance hierarchy — `Phosphor::Screens::ScreenAdaptorBase`
   with `Q_OBJECT` but no `Q_CLASSINFO`, plus per-host subclasses
   (`PlasmaZones::ScreenAdaptor : ScreenAdaptorBase` with the
   `Q_CLASSINFO`). Each host carries its own thin moc-only subclass.
2. A code-generation step that stamps the `Q_CLASSINFO` literal at
   configure time from a CMake variable.
3. Keeping the adaptor per-host, which is where we are now.

The JSON round-trip in `setVirtualScreenConfig(QString physId, QString json)`
is also daemon-specific (it parses the PlasmaZones VS config schema
with backwards-compat fallbacks). A clean lib surface would push that
to a `decodeAndApply` callback the host provides, further widening
the inheritance / factory seam.

None of this work is speculative-clean — it's a real refactor — and
there is no second consumer today to validate the abstraction. This
is a textbook case of "wait for the second example before designing
the interface."

**What it would take to land.** One concrete non-PlasmaZones consumer
(a Wayfire or Hyprland plugin prototype, or a single-process WM) plus
the inheritance scaffolding above. At that point the split is
justified. Until then, `ScreenAdaptor` remains a well-named daemon
file that happens to consume lib types — the mirror image of how
`src/config/settingsconfigstore.{h,cpp}` consumes `IConfigStore`.

### 🟡 Remaining forwarding shims

Five of the six shims the main migration introduced are still in place.
They were kept deliberately — each has specific, narrow reasons. None
are on a "cleanup later" list that would go stale.

| Shim | Canonical target | Callers | Why it stays |
|---|---|---|---|
| `src/core/virtualscreen.h` | `<PhosphorScreens/VirtualScreen.h>` | ~30 in-tree (daemon, KCM, editor, tests) | Re-exports `VirtualScreenDef` / `VirtualScreenConfig` into `PlasmaZones::`. Dropping it is a mechanical sed on both the `#include` path and every `PlasmaZones::VirtualScreenDef` reference, but the churn surface is substantial and the aliases cost nothing at runtime. |
| `src/core/virtualscreenswapper.h` | `<PhosphorScreens/Swapper.h>` | ~10 in-tree (daemon, adaptor, tests) | Same pattern as `virtualscreen.h`. |
| `src/core/screenmanager.h` | (none — the shim is also the service-locator entry point) | ~50 in-tree | NOT a pure shim. It forwards to `screenmanagerservice.h` which provides the daemon's process-global `screenManager()` free function plus the Qt-fallback wrappers for `actualAvailableGeometry` / `isPanelGeometryReady` / `resolvePhysicalScreen` / `effectiveScreenIdsWithFallback`. Removing it would require every caller to include `screenmanagerservice.h` directly. Keeping it at the legacy include path is cheaper and still clean architecturally — the daemon's service-locator is a justified globals-scoped concession that the lib does NOT inherit. |
| `shared/virtualscreenid.h` | `<PhosphorIdentity/VirtualScreenId.h>` | KWin effect + daemon | KWin effect includes it via the top-level `${CMAKE_SOURCE_DIR}` include path and cannot take a direct dependency on phosphor-screens. The shim is effectively a namespace-alias header — two lines of content, zero runtime cost. |
| `src/compositor-common/screen_id.h` | `<PhosphorIdentity/ScreenId.h>` | KWin effect + daemon | Same as above. The KWin effect pulls phosphor-identity transitively via `plasmazones_compositor_common` and reaches the canonical header through this shim. |

`src/core/screen_resolver.h` was dropped because it had exactly one
caller. The scale rule is simple: a shim with one caller isn't worth
keeping; a shim with 30+ callers costs nothing and the cleanup is
best done by the team at whatever cadence suits the codebase's
formatting / commit policy.

## Resolved during implementation

The "open questions" from the initial design — deliberately left
unsettled when the design doc was written — shook out as follows.

- ✅ **Singleton scope — REMOVED from the lib.** The initial design
  said the singleton would stay for 0.x. When the daemon's 107 call
  sites were laid out in detail, eliminating the singleton from the
  **library** turned out to be strictly better than the smell it was
  papering over, because:
    - Singletons in a library force every consumer into the same
      scope model. Two hypothetical future consumers (test harness
      with two fake compositor sessions, multi-process embedded
      shell) couldn't use the lib at all.
    - The singleton's sole **real** consumer is the daemon, which can
      and did re-introduce a thin process-global accessor in its own
      code (`src/core/screenmanagerservice.cpp`) for the legitimate
      "I'm deep in a utility chain and can't plumb a pointer" case.
    - Moving the accessor from the lib to the daemon shifts the
      global-state sin to where it actually makes sense: the host
      that has exactly one and knows it.
- ✅ **Static helper accessibility — same solution.** The
  previously-static helpers (`actualAvailableGeometry`,
  `isPanelGeometryReady`, `resolvePhysicalScreen`,
  `effectiveScreenIdsWithFallback`) are now instance methods on
  `ScreenManager` plus daemon-side free-function wrappers that
  compose the instance method with a Qt-only fallback for "no
  manager registered yet". Call sites that had no `ScreenManager*`
  in scope under the singleton design call the free functions
  without ceremony.
- 🟡 **Plasma D-Bus + Wayfire.** Not yet a real concern because
  there is no Wayfire consumer yet. `IPanelSource` has shipped and
  is small enough to satisfy the design intent — a future Wayfire
  backend drops in alongside `PlasmaPanelSource` without lib-side
  changes.

## Versioning & stability

`0.x` — API may break between minor versions.
`1.0` — API frozen; subsequent minor versions additive-only. Target for 1.0
is once the PlasmaZones migration ships and one non-PlasmaZones consumer
(Phosphor WM prototype, an unrelated tiling shell) has used the library
in anger.
