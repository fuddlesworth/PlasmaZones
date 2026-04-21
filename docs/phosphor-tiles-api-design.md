# PhosphorTiles — API Design Document

## Overview

PhosphorTiles is a Qt6/C++20 library that bundles the autotile algorithm
primitives extracted from PlasmaZones into a self-contained SHARED library.
It provides the runtime tiling engine contract (`TilingAlgorithm`), the
per-screen mutable state container (`TilingState`), the persistent binary
split-tree data structure (`SplitTree`), a process-wide algorithm catalog
(`AlgorithmRegistry`), and — most importantly — the hardened JavaScript
sandbox that lets users ship their own tiling algorithms as `.js` files
without touching C++.

The library does not place windows. It knows nothing about KWin, D-Bus,
compositors, overlays, or the PlasmaZones daemon. It produces `QVector<QRect>`
from a `TilingParams` input, and exposes that machinery both as a C++ API and
as a `PhosphorLayoutApi::ILayoutSource` adapter (`AutotileLayoutSource`) so
editor and settings UIs can render algorithm previews without linking the
daemon.

**License:** LGPL-2.1-or-later (KDE ecosystem standard for libraries)
**Namespace:** `PhosphorTiles`
**Depends on:** Qt6::Core, Qt6::Qml (PRIVATE — for QJSEngine only),
PhosphorLayoutApi (PUBLIC — for `ILayoutSource`/`LayoutPreview`/`EdgeGaps`/
`AlgorithmMetadata`/`GapKeys`)
**Source:** `libs/phosphor-tiles/` — ~5k LOC, 17 public headers, 18 TUs, 22
bundled JS builtins in a Qt resource

---

## Dependency Graph

```
Qt6::Core           PhosphorLayoutApi          PhosphorTiles               PlasmaZones daemon
Qt6::Qml (private)  (ILayoutSource,            (TilingAlgorithm,           (AutotileEngine,
                     LayoutPreview,             AlgorithmRegistry,          overflow policy,
                     EdgeGaps,                  SplitTree,                  settings bridge,
                     GapKeys,                   ScriptedAlgorithm,          per-screen resolver)
                     AlgorithmMetadata)         AutotileLayoutSource)
       │                     │                          │                           │
       └──── PUBLIC link ────┴────────── PUBLIC ────────┘                           │
                                                         └── PUBLIC link ───────────┘

Also usable without PlasmaZones: a headless layout preview tool, a settings
GUI for a different window manager, or a test harness that exercises custom
JS algorithms against synthetic window sets.
```

The dependency direction only flows *outward* from PhosphorTiles. Nothing in
the library includes `src/` headers, links `plasmazones` or
`plasmazones_core`, or references `PhosphorZones` or `PhosphorIdentity`. The
library is a candidate for truly separate distribution once the daemon-side
`AutotileEngine` is carved out into a future `phosphor-tile-engine` library;
the interface contracts here are designed to make that split mechanical
rather than requiring rearchitecture.

---

## Design Principles

1. **Stateless algorithms, stateful engine.** `TilingAlgorithm::calculateZones`
   is `const`. All mutable state (window order, master count, split ratio,
   floating set, persistent split tree) lives in the `TilingState` the engine
   passes in. This lets two daemons / a daemon and an editor run previews
   against the same algorithm instance without locking.
2. **Open algorithm catalog.** `AlgorithmRegistry` accepts runtime
   registrations. Built-ins self-register at static-init time via
   `AlgorithmRegistrar<T>`; user scripts register via `ScriptedAlgorithmLoader`.
   Both paths flow through `registerAlgorithm(id, algo)` — no privileged API.
3. **JavaScript algorithms are first-class.** Shipping a scripted algorithm
   should feel the same as shipping a C++ one — same registry, same preview
   pipeline, same `TilingParams` shape. The library's biggest surface is the
   sandbox that makes that safe.
4. **Security over ergonomics in the sandbox.** When a hardening step and
   an ergonomic JS feature conflict, the hardening wins. `Proxy`, `eval`,
   `Function`, `globalThis`, `Symbol`, `GeneratorFunction`, `AsyncFunction`
   are all disabled or frozen. `WeakMap`/`WeakSet` survive intentionally —
   some algorithms use them for leaf-identity tracking and they don't
   expose a constructor escape.
5. **No daemon concerns leak in.** Overflow behaviour, settings migration,
   per-screen config resolution, and D-Bus adaptors are host-application
   internals and stay in `src/`. The library's job stops at zone rectangles.
6. **Injected resolvers, not injected services.** The one cross-cutting
   concern — "what app class is this window?" — is a
   `std::function<QString(const QString&)>` on `TilingAlgorithm`, not a
   `WindowRegistry*`. Tests plug in canned answers; production wires the
   live registry via `setAppIdResolver` once at engine startup.
7. **Single resource of truth for limits.** `AutotileDefaults` owns every
   numeric constraint. Sandbox constants (`PZ_MIN_ZONE_SIZE`, `PZ_MIN_SPLIT`,
   `PZ_MAX_SPLIT`, `MAX_TREE_DEPTH`) are injected from the same C++
   defaults so C++ algorithms, JS builtins, and serialisation code cannot
   disagree about what a legal split ratio is.
8. **Preview parity.** Every renderer — zone selector, overlay preview,
   OSD, KCM algorithm chooser — goes through the same `previewFromAlgorithm`
   function and the same `PreviewCanvasSize`. A scripted algorithm and a
   built-in one are indistinguishable at the preview boundary.

---

## Public API

### 1. TilingAlgorithm — `PhosphorTiles::TilingAlgorithm`

Abstract base for every tiling algorithm. Subclasses implement `name`,
`description`, and `calculateZones`; virtually everything else has a sensible
default.

```cpp
#include <PhosphorTiles/TilingAlgorithm>

class PHOSPHORTILES_EXPORT TilingAlgorithm : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)
    Q_PROPERTY(bool supportsMasterCount READ supportsMasterCount CONSTANT)
    Q_PROPERTY(bool supportsSplitRatio READ supportsSplitRatio CONSTANT)

public:
    explicit TilingAlgorithm(QObject* parent = nullptr);
    ~TilingAlgorithm() override = default;

    TilingAlgorithm(const TilingAlgorithm&) = delete;
    TilingAlgorithm& operator=(const TilingAlgorithm&) = delete;

    // ── Identity ──────────────────────────────────────────────────────
    virtual QString name() const = 0;
    virtual QString description() const = 0;

    // ── Zone computation (the hot path) ───────────────────────────────
    virtual QVector<QRect> calculateZones(const TilingParams& params) const = 0;

    // ── Capabilities ─────────────────────────────────────────────────
    virtual int      masterZoneIndex()           const;  // -1 if no master concept
    virtual bool     supportsMasterCount()       const;
    virtual bool     supportsSplitRatio()        const;
    virtual qreal    defaultSplitRatio()         const;
    virtual int      minimumWindows()            const;
    virtual int      defaultMaxWindows()         const;
    virtual bool     producesOverlappingZones()  const;
    virtual bool     centerLayout()              const;
    virtual QString  zoneNumberDisplay()         const noexcept;
    virtual bool     isScripted()                const noexcept;
    virtual bool     isUserScript()              const noexcept;
    virtual bool     supportsMinSizes()          const noexcept;
    virtual bool     supportsMemory()            const noexcept;

    // ── Lifecycle hooks (v2) ─────────────────────────────────────────
    virtual bool supportsLifecycleHooks() const noexcept;
    virtual void prepareTilingState(TilingState* state) const;
    virtual void onWindowAdded(TilingState* state, int windowIndex);
    virtual void onWindowRemoved(TilingState* state, int windowIndex);

    // ── Custom algorithm parameters (v2) ─────────────────────────────
    virtual bool         supportsCustomParams() const noexcept;
    virtual QVariantList customParamDefList()   const;
    virtual bool         hasCustomParam(const QString& name) const;

    // ── App-id resolver (injected once at engine startup) ────────────
    void setAppIdResolver(std::function<QString(const QString&)> resolver);
    std::function<QString(const QString&)> appIdResolver() const;

    // ── Registry back-reference (populated by AlgorithmRegistry) ─────
    QString registryId() const;
    void    setRegistryId(const QString& id);

Q_SIGNALS:
    void configurationChanged();

protected:
    // Pixel-perfect distribution helpers shared by every built-in algorithm.
    // Public enough for scripted algorithms to import through the JS
    // builtin shims (see ScriptedAlgorithmJsBuiltins), internal enough
    // that consumers don't reach into them directly.
    static QVector<int> distributeEvenly(int total, int count);
    static QRect        innerRect(const QRect& screenGeometry, int outerGap);
    static QRect        innerRect(const QRect& screenGeometry, const EdgeGaps& gaps);
    static QVector<int> distributeWithGaps(int total, int count, int gap);
    static QVector<int> distributeWithMinSizes(int total, int count, int gap,
                                               const QVector<QSize>& minDims);
    static int          minWidthAt (const QVector<QSize>& minSizes, int index);
    static int          minHeightAt(const QVector<QSize>& minSizes, int index);
    static void         solveTwoPartMinSizes(int contentDim, int& firstDim, int& secondDim,
                                             int minFirst, int minSecond);
    static void         applyPerWindowMinSize(int& w, int& h,
                                              const QVector<QSize>& minSizes, int index);
    static ThreeColumnWidths  solveThreeColumnWidths(/* … */);
    static CumulativeMinDims  computeAlternatingCumulativeMinDims(int windowCount,
                                                                  const QVector<QSize>& minSizes,
                                                                  int innerGap);
    static void              appendGracefulDegradation(QVector<QRect>& zones,
                                                       const QRect& remaining,
                                                       int leftover, int innerGap);
    static qreal             clampOrProportionalFallback(qreal ratio,
                                                         qreal minFirstRatio, qreal maxFirstRatio,
                                                         int firstDim, int secondDim);
};
```

#### Thread safety contract

All `const` algorithm methods are safe to invoke concurrently on the same
instance. `TilingState` must not mutate during the call — the engine owns
that coordination. `setAppIdResolver` and `setRegistryId` are main-thread
only; the library asserts on `registerAlgorithm` that the call happens on
the thread that owns the QCoreApplication.

#### noexcept convention

`supportsMinSizes`, `supportsMemory`, `zoneNumberDisplay`, `isScripted`,
`isUserScript`, and `supportsLifecycleHooks` / `supportsCustomParams` are
`noexcept` because they read cached POD fields only. Everything else —
including the capability getters that a scripted algorithm may override
with a JS function — is intentionally *not* noexcept: `ScriptedAlgorithm`
may need to call through to QJSEngine to resolve the override.

#### Why lifecycle hooks are mutable-state

`calculateZones` takes `const TilingParams&` (immutable contract). Hooks
receive a mutable `TilingState*` so memory algorithms (dwindle-memory,
bsp-memory) can insert/remove leaves in their persistent `SplitTree`
*before* the retile rather than rebuilding the tree from scratch on every
calculation. The engine documents that `onWindowRemoved` fires with the
departing window *still present* in the state — tree mutations target that
index, then the engine drops it from the window order.

---

### 2. TilingParams — `PhosphorTiles::TilingParams`

The full input bundle for `calculateZones`. A struct rather than a parameter
list so v2+ fields (custom params, screen info, focused index, per-window
metadata) can land without rippling into the virtual signature.

```cpp
struct TilingParams
{
    int                 windowCount      = 0;
    QRect               screenGeometry;
    const TilingState*  state            = nullptr;   // REQUIRED — non-null
    int                 innerGap         = 0;
    EdgeGaps            outerGaps;                    // per-side, from PhosphorLayoutApi
    QVector<QSize>      minSizes;                     // may be empty

    // v2 enriched context
    QVector<WindowInfo> windowInfos;                  // parallel to window order
    int                 focusedIndex     = -1;
    TilingScreenInfo    screenInfo;                   // portrait flag, aspect
    QVariantMap         customParams;                 // @param declarations

    static TilingParams forPreview(int count, const QRect& rect,
                                   const TilingState* state);
};

struct WindowInfo       { QString appId; bool focused = false; };
struct TilingScreenInfo { QString id; bool portrait = false; qreal aspectRatio = 0.0; };
```

`state` is documented non-null and algorithms may dereference without
checking. This is enforced at the engine's construction site: a
`TilingState` is always created before any algorithm runs. The
`forPreview` helper produces a valid `TilingParams` for stateless preview
rendering — `state` comes from a transient `TilingState` the preview
builder owns for the call.

`buildWindowInfos(state, windowCount, appIdResolver, focusedIndex)` is the
shared builder used by both the engine (when assembling params) and
`ScriptedAlgorithm` (when building the JS state object for hook calls).
Tests plug in a canned resolver; production passes
`[this](const QString& id) { return m_windowRegistry->currentClassFor(id); }`.

---

### 3. TilingState — `PhosphorTiles::TilingState`

Per-screen mutable state: window order, master count, split ratio, floating
set, focused window, last calculated zones, and an optional persistent
`SplitTree` for memory algorithms.

```cpp
class PHOSPHORTILES_EXPORT TilingState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString screenId       READ screenId        CONSTANT)
    Q_PROPERTY(int     windowCount    READ windowCount     NOTIFY windowCountChanged)
    Q_PROPERTY(int     tiledWindowCount READ tiledWindowCount NOTIFY windowCountChanged)
    Q_PROPERTY(int     masterCount    READ masterCount     WRITE setMasterCount
                                       NOTIFY masterCountChanged)
    Q_PROPERTY(qreal   splitRatio     READ splitRatio      WRITE setSplitRatio
                                       NOTIFY splitRatioChanged)

public:
    explicit TilingState(const QString& screenId, QObject* parent = nullptr);

    // Window order
    int          windowCount() const;
    int          tiledWindowCount() const;
    QStringList  windowOrder() const;
    QStringList  tiledWindows() const;
    bool         addWindow(const QString& id, int position = -1);
    bool         removeWindow(const QString& id);
    bool         moveWindow(int fromIndex, int toIndex);
    bool         swapWindows(int index1, int index2);
    bool         swapWindowsById(const QString& id1, const QString& id2);
    bool         rotateWindows(bool clockwise = true);

    // Master
    int          masterCount() const;
    void         setMasterCount(int count);
    QStringList  masterWindows() const;
    QStringList  stackWindows()  const;
    bool         promoteToMaster(const QString& id);
    bool         insertAfterFocused(const QString& id);

    // Split ratio
    qreal        splitRatio() const;
    void         setSplitRatio(qreal ratio);
    void         increaseSplitRatio(qreal delta = 0.05);
    void         decreaseSplitRatio(qreal delta = 0.05);

    // Floating
    bool         isFloating(const QString& id) const;
    void         setFloating(const QString& id, bool floating);
    bool         toggleFloating(const QString& id);
    QStringList  floatingWindows() const;

    // Focus
    QString      focusedWindow() const;
    void         setFocusedWindow(const QString& id);
    int          focusedTiledIndex() const;

    // Serialization
    QJsonObject                      toJson() const;
    static TilingState*              fromJson(const QJsonObject& json,
                                              QObject* parent = nullptr);
    void                             clear();

    // Calculated zones (cached by engine after calculateZones)
    void              setCalculatedZones(const QVector<QRect>& zones);
    QVector<QRect>    calculatedZones() const;

    // Persistent split tree (memory algorithms)
    SplitTree*        splitTree() const;
    void              setSplitTree(std::unique_ptr<SplitTree> tree);
    void              clearSplitTree();
    void              rebuildSplitTree();

Q_SIGNALS:
    void windowCountChanged();
    void windowOrderChanged();
    void masterCountChanged();
    void splitRatioChanged();
    void floatingChanged(const QString& id, bool floating);
    void focusedWindowChanged();
    void stateChanged();   // coalesced "something requires retile"
};
```

Clamping is centralised via `clampMasterCount` and `clampSplitRatio` private
statics so that `setMasterCount`, `setSplitRatio`, and `fromJson` all agree
on the legal range — no chance for a serialised-and-reloaded state to carry
an out-of-bounds value that the setters would reject.

Tree sync helpers (`syncTreeInsert`, `syncTreeRemove`, `syncTreeSwap`,
`syncTreeLazyCreate`) ensure the optional `SplitTree` stays coherent with
the window order when one is present. When it isn't, the helpers no-op —
memory algorithms lazy-create the tree via `prepareTilingState`.

---

### 4. SplitTree — `PhosphorTiles::SplitTree`

A persistent binary split tree where each leaf is a window and each internal
node defines how its area is divided between two children. Mutated
incrementally by memory algorithms; can round-trip through JSON so tiling
state survives a daemon restart.

```cpp
struct PHOSPHORTILES_EXPORT SplitNode {
    qreal        splitRatio       = AutotileDefaults::DefaultSplitRatio;
    bool         splitHorizontal  = false;  // true = top/bottom, false = left/right
    std::unique_ptr<SplitNode> first;       // left or top
    std::unique_ptr<SplitNode> second;      // right or bottom
    SplitNode*   parent           = nullptr;  // non-owning back-pointer
    QString      windowId;                  // non-empty on leaves only
    bool         isLeaf() const;
};

class PHOSPHORTILES_EXPORT SplitTree
{
public:
    SplitTree();
    SplitTree(SplitTree&& other) noexcept;
    SplitTree& operator=(SplitTree&& other) noexcept;
    ~SplitTree();
    SplitTree(const SplitTree&)            = delete;
    SplitTree& operator=(const SplitTree&) = delete;

    // Queries
    const SplitNode* root()    const noexcept;
    SplitNode*       root()          noexcept;
    bool             isEmpty() const noexcept;
    int              leafCount() const noexcept;
    int              treeHeight() const noexcept;
    const SplitNode* leafForWindow(const QString& id) const;
    QStringList      leafOrder() const;

    // Mutations
    void  insertAtFocused (const QString& id, const QString& focusedId,
                           qreal initialRatio = 0.0);
    void  insertAtEnd     (const QString& id, qreal initialRatio = 0.0);
    void  insertAtPosition(const QString& id, int position,
                           qreal initialRatio = 0.0);
    void  remove(const QString& id);
    void  swap  (const QString& id1, const QString& id2);
    bool  swapLeaves(const QString& a, const QString& b);
    void  resizeSplit(const QString& id, qreal newRatio);

    // Geometry
    QVector<QRect> applyGeometry(const QRect& area, int innerGap) const;
    bool           rebuildFromOrder(const QStringList& tiledWindows,
                                    qreal defaultSplitRatio =
                                        AutotileDefaults::DefaultSplitRatio);

    // Serialization
    QJsonObject                     toJson() const;
    static std::unique_ptr<SplitTree> fromJson(const QJsonObject& json);
};
```

Serialisation is tightened against adversarial JSON: max 1024 nodes, max
depth `MaxRuntimeTreeDepth` (50), split ratios clamped on load, duplicate
window ids rejected. The split-tree JSON round-trip is what lets tiling
state persist across daemon restarts — `TilingState::toJson` recursively
calls `SplitTree::toJson` and `TilingState::fromJson` recursively calls
`SplitTree::fromJson`.

The graceful-degradation behaviour of `applyGeometry` (emitting 1×1 rects
for leaves that can't fit inside the gap budget) is a load-bearing contract:
downstream code maps tiled windows to zones positionally, so the returned
rect count must equal `leafCount()` even when the area is too small. A
`qCWarning` on the library-local `lcTilesLib` category surfaces the
degradation.

---

### 5. AlgorithmRegistry — `PhosphorTiles::AlgorithmRegistry`

Process-wide catalog of tiling algorithms. Singleton-per-process (Meyer's
local-static), connects to `QCoreApplication::aboutToQuit` for deterministic
teardown.

```cpp
class PHOSPHORTILES_EXPORT AlgorithmRegistry : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AlgorithmRegistry)

public:
    static AlgorithmRegistry* instance();      // Meyer's singleton

    // Lifecycle
    void     cleanup();                         // connected to aboutToQuit

    // Registration
    void     registerAlgorithm(const QString& id, TilingAlgorithm* algorithm);
    bool     unregisterAlgorithm(const QString& id);

    // Access
    TilingAlgorithm*         algorithm(const QString& id) const;
    QStringList              availableAlgorithms() const noexcept;
    QList<TilingAlgorithm*>  allAlgorithms() const;
    bool                     hasAlgorithm(const QString& id) const noexcept;
    static QString           defaultAlgorithmId();            // "bsp"
    TilingAlgorithm*         defaultAlgorithm() const;

    // Preview configuration
    static constexpr int PreviewCanvasSize = 1000;

    struct PreviewParams {
        QString  algorithmId;                   // currently-active algorithm
        int      maxWindows  = -1;              // -1 = algorithm default
        int      masterCount = -1;              // -1 = default 1
        qreal    splitRatio  = -1.0;            // -1 = algorithm default
        QHash<QString, QVariantMap> savedAlgorithmSettings;   // per-alg saved state
        bool operator==(const PreviewParams& other) const;
    };
    void                    setPreviewParams(const PreviewParams& params);
    const PreviewParams&    previewParams() const noexcept;

    // Static backwards-compat shims
    static void                 setConfiguredPreviewParams(const PreviewParams& p);
    static const PreviewParams& configuredPreviewParams();

Q_SIGNALS:
    void algorithmRegistered(const QString& id);
    void algorithmUnregistered(const QString& id, bool replacing);
    void previewParamsChanged();
};
```

#### Algorithm id rules

IDs flow into `LayoutPreview::id` (`"autotile:<id>"`), JSON keys, D-Bus
method arguments, and QML model roles. `registerAlgorithm` rejects any id
that isn't `[A-Za-z0-9._:-]+` before the id can reach a downstream parser
that expects ASCII. The `:` separator enables namespacing — scripted
algorithms register as `"script:<basename>"` unless their `id`
metadata provides an explicit id.

#### Registration ordering

Built-in algorithms self-register at static initialisation via
`AlgorithmRegistrar<T>`, which appends to the process-wide
`pendingAlgorithmRegistrations()` list. The registry constructor drains
that list via `registerBuiltInAlgorithms` sorted by priority (lower ==
earlier). User scripts register later through
`ScriptedAlgorithmLoader::scanAndRegister`, which calls
`registerAlgorithm(id, scripted)` for each `.js` file found.

Re-registration is allowed: the old algorithm is dropped via
`deleteLater()` and the new one takes its slot. The signal order is
`algorithmUnregistered(id, true) → algorithmRegistered(id)` and the new
algorithm is already queryable when either signal fires.

#### Why a singleton

Every process that consumes the library wants exactly one algorithm
catalog. Daemon, editor, settings app, and KCM each instantiate their own
`AlgorithmRegistry` — they don't share across process boundaries. Inside
a process, scattering registrations across multiple registry instances
would just be dependency injection theatre: the scripted-algorithm loader
would still need a well-known handle to register into, and every preview
callsite would need to know which registry owns the id it wants to
resolve. The singleton keeps those callsites honest. Consumers who want
isolation for tests construct a bare `AutotileLayoutSource(ownRegistry)`
and bypass `instance()`.

#### Why teardown is wired to `aboutToQuit`

`ScriptedAlgorithm` holds a `QJSEngine`, which is a QObject that requires
a live Qt runtime to destruct cleanly. A Meyer's singleton outlives
`QCoreApplication` — so unless we evict the engines before
`QCoreApplication` exits, static destruction crashes in `~QJSEngine`.
`cleanup()` drains pending `deleteLater()` posts, then `delete`s each
algorithm directly (not `deleteLater` — the event loop is already gone).

---

### 6. ScriptedAlgorithm — `PhosphorTiles::ScriptedAlgorithm`

A `TilingAlgorithm` subclass that loads its body from a JavaScript file
evaluated in a hardened `QJSEngine` sandbox. The single most complex piece
of the library.

```cpp
class PHOSPHORTILES_EXPORT ScriptedAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit ScriptedAlgorithm(const QString& filePath,
                               QObject* parent = nullptr);
    ~ScriptedAlgorithm() override;

    bool     isValid()    const;   // script loaded + exports calculateZones
    QString  filePath()   const;
    QString  scriptId()   const;   // "script:<basename>"
    QString  id()  const;          // from metadata id, if present
    void     setUserScript(bool isUser);

    // TilingAlgorithm overrides
    QString  name()        const override;
    QString  description() const override;
    QVector<QRect> calculateZones(const TilingParams& params) const override;
    // … every capability method routes through resolveJsOverride() …

    const QVector<ScriptedHelpers::CustomParamDef>& customParamDefs() const;

    // Called from the shared watchdog thread when a guarded call
    // exceeds its deadline. Forwards to QJSEngine::setInterrupted.
    void interruptEngine();
};
```

#### Script authoring

A script is a single `.js` file. Required exports:

- `calculateZones(params) -> [{x, y, width, height}, ...]`

Optional JS exports (override capability getters dynamically):

- `masterZoneIndex`, `supportsMasterCount`, `supportsSplitRatio`,
  `defaultSplitRatio`, `minimumWindows`, `defaultMaxWindows`,
  `producesOverlappingZones`, `centerLayout`

Optional lifecycle hooks (v2):

- `onWindowAdded(state, index)`, `onWindowRemoved(state, index)`

Metadata lives in a JS-exported `var metadata = { ... }` object. The
parser recognises `name`, `description`, `supportsMasterCount`,
`supportsSplitRatio`, `producesOverlappingZones`, `centerLayout`,
`zoneNumberDisplay`, `defaultSplitRatio`, `defaultMaxWindows`,
`minimumWindows`, `masterZoneIndex`, `supportsMemory`, `supportsMinSizes`,
`id`, and `customParams` declarations.

`customParams` declares custom algorithm parameters that flow through as
`params.custom.<name>` in the JS, and show up in the settings UI via
`customParamDefList()`:

```js
var metadata = {
    name: "My Algorithm",
    // ... other metadata fields ...
    customParams: [
        { name: "speed", type: "number", default: 0.1, min: 0.01, max: 0.5, description: "Animation speed" },
        { name: "mode", type: "enum", default: "balanced", options: ["compact", "balanced", "wide"], description: "Layout mode" }
    ]
};
```

#### Sandbox hardening

`ScriptedAlgorithmSandbox::hardenSandbox(QJSEngine*)` is applied *before*
any builtin or user code is evaluated. The hardening is layered:

1. Disable `eval`. CRITICAL — failure aborts the load.
2. Freeze `Function.prototype.constructor`, then `Function.prototype`,
   then the `Function` global itself. Ordering matters: the freeze on the
   prototype must happen before `Function` is disabled (which makes
   `Function.prototype` unreachable), and the prototype freeze must happen
   before the constructor lockdown on individual built-ins so the escape
   path is closed the moment the freeze lands.
3. Freeze generator/async constructors. `GeneratorFunction` is critical;
   `AsyncFunction` is critical unless the engine returned a `SyntaxError`
   when asked to parse an async function (V4/ES5 without async support —
   `AsyncFunction` is unreachable, no lockdown needed).
4. Freeze the prototypes of `Object`, `Array`, `String`, `Number`,
   `Boolean`, `RegExp`, `Date`, `Error`, `Map`, `Set` — prototype pollution
   is a classic sandbox-escape primitive.
5. Close the `.constructor` → `Function` escape on 20 built-ins. Each
   `defineProperty` call is wrapped in a per-constructor try/catch so a
   single already-non-configurable descriptor can't abort the whole
   lockdown; constructors absent on the engine (e.g. `WeakRef` on V4) are
   skipped via the `typeof` guard.
6. Freeze `Object`, `Array`, `JSON`, `Math` so scripts can't shadow
   `Object.freeze` / `Object.defineProperty` themselves.
7. Disable `Proxy` (critical — intercepts property access and constructs
   escapes), `Reflect`, `WeakRef`, `FinalizationRegistry`.
8. Disable `globalThis` (critical — otherwise every lockdown above is
   defeated via `globalThis.Function`).
9. Disable `Symbol` (critical — `Symbol.toPrimitive` enables
   type-confusion attacks).
10. Strip the QJSEngine-provided `Qt`, `qsTr`, `qsTrId`, `print`,
    `console`, `setTimeout`, `setInterval`, `clearTimeout`,
    `clearInterval`, `gc`, and `import` globals.

`WeakMap` and `WeakSet` intentionally survive — some algorithms use them
for leaf-identity tracking and they don't expose a `.constructor` escape
once the built-in constructor lockdown has run.

After hardening, `loadScript` injects the pre-frozen global constants
(`PZ_MIN_ZONE_SIZE`, `PZ_MIN_SPLIT`, `PZ_MAX_SPLIT`, `MAX_TREE_DEPTH`) via
`QJSValue::setProperty` — *not* via stitched JS source, to avoid
locale-dependent number formatting (e.g. `"0,5"` under `de_DE`) and
type-drift between C++ and the parser.

#### Builtin injection sequence

22 JS builtins ship in `:/builtins/` (Qt resource from `builtins.qrc`).
They are ports of the C++ `TilingAlgorithm` protected helpers, so
scripted algorithms get the same pixel-perfect distribution,
min-size solving, and graceful-degradation behaviour as the built-ins.

Injection order is topologically sorted by dependency — each builtin is
evaluated *after* the builtins it references, and *immediately frozen*
after evaluation (via `Object.defineProperty(this, 'name', {writable:
false, configurable: false})`) so a later builtin or the user script
cannot shadow it:

```
clampSplitRatio         (standalone, utility)
applyTreeGeometry       (standalone)
lShapeLayout            (standalone)
distributeEvenly        (standalone)
distributeWithGaps      (standalone)
distributeWithMinSizes  → distributeWithGaps
distributeWithOptionalMins
                        → distributeWithGaps, distributeWithMinSizes
solveTwoPart            (standalone)
solveThreeColumn        (uses PZ_* constants)
computeCumulativeMinDims (standalone)
appendGracefulDegradation → distributeWithGaps
dwindleLayout           → computeCumulativeMinDims, appendGracefulDegradation
extractMinDims          (standalone, exports extractMinWidths/Heights/_extractMinDims)
interleaveStacks        (standalone, exports 4 helpers)
applyPerWindowMinSize   (standalone)
extractRegionMaxMin     (standalone)
fillRegion              (standalone)
fillArea                → fillRegion
deckLayout              → fillArea
masterStackLayout       → fillArea, extractRegionMaxMin, solveTwoPart,
                          extractMinDims, distributeWithOptionalMins
equalColumnsLayout      → extractMinDims, distributeWithOptionalMins
threeColumnLayout       → fillArea, extractRegionMaxMin, solveThreeColumn,
                          extractMinDims, interleaveStacks,
                          distributeWithOptionalMins
```

A missing export in any `injectAndFreeze` call would be a silent sandbox
escape (user code could shadow the helper), so each builtin's exports
list is enumerated explicitly in the injection code. Freezing in one
batch at the end would leave a window where an already-injected helper
could be overwritten by a later one — per-step freeze closes the gap.

#### User script wrapping

After builtins load, the user script is wrapped in an IIFE that shadows
`eval`, `Function`, `AsyncFunction`, `GeneratorFunction`, and
`AsyncGeneratorFunction` with `undefined` via parameter binding:

```js
(function(eval, Function, AsyncFunction, GeneratorFunction, AsyncGeneratorFunction) {
  // user script body
  if (typeof calculateZones === 'function') this.calculateZones = calculateZones;
  if (typeof masterZoneIndex === 'function') this.masterZoneIndex = masterZoneIndex;
  // … optional overrides …
}).call(this, void 0, void 0, void 0, void 0, void 0);
```

This is defense-in-depth on top of the sandbox property locks. QJSEngine's
V4 treats direct `eval()` as a language-level built-in that bypasses
`Object.defineProperty` on the global, so property-level lockdown alone is
insufficient — the IIFE scoping ensures the names resolve to `undefined`
within the user scope regardless.

#### Watchdog

Every guarded JS call is bracketed by `ScriptedAlgorithmWatchdog::arm(this,
timeoutMs)` → evaluate → `disarm(this)`. One OS thread services every live
`ScriptedAlgorithm` instance process-wide (replaced the legacy "one thread
per algorithm" model). The deadline is `AutotileDefaults::
ScriptWatchdogTimeoutMs` (100 ms — generous enough for ARM and first-call
JIT warm-up on slow systems).

The disarm-race is the subtle part. A naive design would delete the entry
on disarm; if the watchdog thread was already inside its `wait_until`
predicate when disarm fired, it could dereference a freed entry. The
library uses a **generation counter** pattern instead:

- `arm()` bumps the generation and sets `deadline = now() + timeout`.
- `disarm()` bumps the generation and sets `deadline = time_point::max()`
  (sentinel that the thread's "find earliest deadline" loop ignores).
- The watchdog thread's `wait_until` predicate captures the generation it
  observed when picking the next deadline; on wake-up it compares against
  the current entry's generation. If it changed, disarm or re-arm
  happened and the thread just loops. Only a match fires the interrupt.
- Deadline computation happens *inside* the locked section, not before
  taking the mutex — otherwise lock contention could push the stored
  deadline into the past and the watchdog would fire immediately on a
  script that never actually timed out.
- `unregister()` (from `~ScriptedAlgorithm`) erases the entry under the
  mutex, which is also the mutex the watchdog holds while calling
  `interruptEngine`. That's how the instance stays alive for the
  duration of the interrupt call.

`QJSEngine::setInterrupted(true)` is documented thread-safe relative to
the target engine's evaluation thread; the interrupt aborts the running
script and `evaluate()` returns with an error, which `calculateZones`
catches via the `m_lastCallTimedOut` flag and falls back to a safe empty
zone list.

---

### 7. ScriptedAlgorithmLoader — `PhosphorTiles::ScriptedAlgorithmLoader`

Discovers `.js` files in XDG data directories, instantiates
`ScriptedAlgorithm` for each, and registers them with `AlgorithmRegistry`.
Watches for changes via `QFileSystemWatcher` with debounced reloads.

```cpp
class PHOSPHORTILES_EXPORT ScriptedAlgorithmLoader : public QObject
{
    Q_OBJECT
public:
    explicit ScriptedAlgorithmLoader(const QString& subdirectory,
                                     QObject* parent = nullptr);

    bool        scanAndRegister();
    void        ensureUserDirectoryExists();
    QString     userAlgorithmDir() const;

Q_SIGNALS:
    void algorithmsChanged();
};
```

The `subdirectory` is injected at construction — the library is
brand-agnostic. PlasmaZones passes `"plasmazones/algorithms"`; another
window manager consuming the library could pass its own.

Directory resolution walks every `QStandardPaths::GenericDataLocation`
entry: system dirs first, then user (`writableLocation`) last. A user
file with the same basename as a system file overrides it at the registry
level — the loader unregisters the system script id before registering
the user one, then flags `isUserScript(true)`.

The debounce timer defaults to `RefreshDebounceMs = 500`. A second
follow-up rescan fires after `FollowupRescanMs = 500` to catch edits
that land inside the no-watcher window created by atomic-replace (new
inode) between `onFileChanged` dropping the watch and `reWatchFiles()`
re-adding it.

---

### 8. AutotileLayoutSource — `PhosphorTiles::AutotileLayoutSource`

Adapts `AlgorithmRegistry` to the `PhosphorLayoutApi::ILayoutSource`
contract so editor / settings / overlay code can iterate autotile previews
uniformly with manual zone previews from `PhosphorZones::ZonesLayoutSource`.
Both adapt to `LayoutPreview`; consumers branch on `LayoutPreview::
isAutotile` rather than on which concrete source they hold.

```cpp
class PHOSPHORTILES_EXPORT AutotileLayoutSource : public PhosphorLayout::ILayoutSource
{
    Q_OBJECT
public:
    explicit AutotileLayoutSource(
        PhosphorTiles::AlgorithmRegistry* registry = nullptr,  // nullptr = instance()
        QObject* parent = nullptr);

    QVector<PhosphorLayout::LayoutPreview> availableLayouts() const override;

    // id must be "autotile:<algorithmId>" — returns empty preview otherwise.
    // canvas is ignored; algorithms produce relative zones.
    PhosphorLayout::LayoutPreview previewAt(const QString& id,
        int windowCount = PhosphorLayout::DefaultPreviewWindowCount,
        const QSize& canvas = {}) override;

    void invalidateCache();
};
```

#### Reactive wiring

At construction, the source connects to three registry signals — all of
them flow into a single `invalidateCache()` slot so every invalidation
path emits `contentsChanged()` exactly once:

- `algorithmRegistered` — new algorithm to iterate
- `algorithmUnregistered` — algorithm is gone
- `previewParamsChanged` — user tuned master-count / split-ratio / saved
  per-algorithm settings

Merging the three into one slot means the emit is owned by a single
function; the lambdas just forward. Consumers see exactly one
`contentsChanged` per event.

#### Cache shape

The cache key is `"<algorithmId>|<windowCount>"`. FIFO eviction bounds
memory at `availableAlgorithms().size() * 10` entries (floor 10) — enough
headroom for the layout-picker UI (one preview per algorithm × a handful
of window counts) without unbounded growth if a caller probes
`previewAt()` with a wide range. The algorithm count is cached
(`m_algorithmCountCache`) and refreshed on every invalidate, so
`insertCacheEntry` doesn't re-query the registry on every hit.

#### Preview params fallback chain

`previewFromAlgorithm(id, algorithm, windowCount, registry)` consults
`registry->previewParams()` to pick master-count / split-ratio / window
count:

1. If `algorithm` is the *active* one (matches `params.algorithmId`),
   read the top-level `masterCount` / `splitRatio` / `maxWindows` — these
   reflect the live user-facing configuration.
2. Else fall back to `params.savedAlgorithmSettings[algorithmId]` — the
   per-algorithm saved entry, populated when the user switches away.
3. Else use the algorithm's own `defaultSplitRatio()` / `defaultMasterCount`
   / `defaultMaxWindows()`.

Empty `windowCount` means "use the effective max". The canvas size is
always `PreviewCanvasSize` (1000 × 1000) so every renderer scales against
the same unit rect.

---

### 9. AutotileConstants — `PhosphorTiles::AutotileDefaults` / `AutotileJsonKeys` / `AutotileJsonValues`

Library-owned constants with no cross-layer dependencies.
`AutotileDefaults` is the single source of truth for every numeric limit
the library enforces:

```cpp
namespace AutotileDefaults {
    constexpr qreal DefaultSplitRatio       = 0.5;
    constexpr int   DefaultMasterCount      = 1;
    constexpr int   DefaultMaxWindows       = 5;
    constexpr qreal MinSplitRatio           = 0.1;
    constexpr qreal MaxSplitRatio           = 0.9;
    constexpr int   MinMasterCount          = 1;
    constexpr int   MaxMasterCount          = 5;
    constexpr int   MinGap                  = 0;
    constexpr int   MaxGap                  = 50;
    constexpr int   MinZoneSizePx           = 50;
    constexpr int   GapEdgeThresholdPx      = 5;
    constexpr int   MinMaxWindows           = 1;
    constexpr int   MaxMaxWindows           = 12;
    constexpr int   UnlimitedMaxWindowsSentinel = std::numeric_limits<int>::max() / 2;
    constexpr int   MaxZones                = 256;
    constexpr int   MaxRuntimeTreeDepth     = 50;
    constexpr int   MaxTreeNodesForJs       = MaxZones * 2;
    constexpr qreal SplitRatioHysteresis    = 0.05;
    constexpr int   ScriptWatchdogTimeoutMs = 100;
    // … plus metadata / animation bounds …
}
```

`MinZoneSizePx`, `MinSplitRatio`, `MaxSplitRatio`, and `MaxRuntimeTreeDepth`
are the four that round-trip through the sandbox as `PZ_MIN_ZONE_SIZE`,
`PZ_MIN_SPLIT`, `PZ_MAX_SPLIT`, and `MAX_TREE_DEPTH` respectively. Keeping
all four in one header is the invariant that makes "C++ and JS agree about
what a legal split ratio is" verifiable by inspection.

`AutotileJsonKeys` and `AutotileJsonValues` own every JSON key string used
by `TilingState` and `AutotileConfig` serialisation. Per-side outer-gap
keys (`OuterGapTop/Bottom/Left/Right`, `UsePerSideOuterGap`) are re-exported
from `PhosphorLayoutApi/GapKeys.h` so `phosphor-zones` and `phosphor-tiles`
share a single source of truth for those particular keys — the layout-api
layer is strictly below both libraries in the dependency DAG.

---

## Injected Interfaces

### App-id resolver

```cpp
using AppIdResolver = std::function<QString(const QString&)>;
void TilingAlgorithm::setAppIdResolver(AppIdResolver resolver);
```

The only cross-cutting concern the library surfaces. `TilingState`'s
`m_windowOrder` contains bare instance ids (daemon-internal opaque
strings); scripted algorithms expose the *live app class* to user JS so
an author can write rules like "firefox always master". The resolver
maps instance id → live class; it's a `std::function` rather than a
`WindowRegistry*` so tests can plug in canned answers without
constructing a real registry and so the library never drags a daemon
header in.

Production wiring (`AutotileEngine::setWindowRegistry`) seeds every
algorithm returned from `AlgorithmRegistry::algorithm()` with the same
live lookup before any lifecycle hook fires. Unset resolvers return a
no-op lambda (empty string), so callsites can invoke it unconditionally.

### PhosphorLayoutApi dependencies

The library consumes four types from PhosphorLayoutApi:

| Type | Role |
|------|------|
| `ILayoutSource` | Base class for `AutotileLayoutSource` |
| `LayoutPreview` | Return type for `previewFromAlgorithm` |
| `AlgorithmMetadata` | `LayoutPreview` field built by `buildMetadata` |
| `EdgeGaps` | `TilingParams::outerGaps` field |
| `GapKeys` | Shared JSON key strings re-exported via `AutotileJsonKeys` |
| `ZoneNumberDisplay` | Enum used by `ScriptMetadata` |

No other layer-api concepts cross the boundary.

---

## Composition Example (what the daemon writes)

```cpp
// Once at daemon startup — composition root
auto* registry = PhosphorTiles::AlgorithmRegistry::instance();

// Built-ins already self-registered at static init.
// Load scripted algorithms from the user's algorithm directory.
auto scriptLoader = std::make_unique<PhosphorTiles::ScriptedAlgorithmLoader>(
    QStringLiteral("plasmazones/algorithms"));
scriptLoader->ensureUserDirectoryExists();
scriptLoader->scanAndRegister();

// Wire app-id resolver so every algorithm sees live window classes.
for (auto* algo : registry->allAlgorithms()) {
    algo->setAppIdResolver([this](const QString& instanceId) {
        return m_windowRegistry->currentClassFor(instanceId);
    });
}

// Push user settings into the registry so previews reflect tuning.
PhosphorTiles::AlgorithmRegistry::PreviewParams params;
params.algorithmId  = m_settings->algorithmId();
params.maxWindows   = m_settings->maxWindows();
params.masterCount  = m_settings->masterCount();
params.splitRatio   = m_settings->splitRatio();
params.savedAlgorithmSettings = m_settings->savedPerAlgorithmSettings();
registry->setPreviewParams(params);

// Per-screen: maintain a TilingState and retile on demand.
auto state = new PhosphorTiles::TilingState(QStringLiteral("HDMI-1"), this);
state->addWindow(QStringLiteral("org.kde.konsole|<uuid>"));
state->addWindow(QStringLiteral("firefox|<uuid>"));

auto* algo = registry->algorithm(QStringLiteral("master-stack"));
algo->prepareTilingState(state);

PhosphorTiles::TilingParams p;
p.windowCount    = state->tiledWindowCount();
p.screenGeometry = screenRect;
p.state          = state;
p.innerGap       = 8;
p.outerGaps      = {8, 8, 8, 8};
int focusedIdx = -1;
p.windowInfos = PhosphorTiles::buildWindowInfos(state, p.windowCount,
                                                algo->appIdResolver(),
                                                focusedIdx);
p.focusedIndex = focusedIdx;

const QVector<QRect> zones = algo->calculateZones(p);
state->setCalculatedZones(zones);
// Daemon applies zones to windows — library's job ends here.
```

And what the settings app writes (no daemon needed):

```cpp
auto* registry = PhosphorTiles::AlgorithmRegistry::instance();
// Built-ins self-register; scripted algorithms are discovered the same way.
auto scriptLoader = std::make_unique<PhosphorTiles::ScriptedAlgorithmLoader>(
    QStringLiteral("plasmazones/algorithms"));
scriptLoader->scanAndRegister();

auto layoutSource = std::make_unique<PhosphorTiles::AutotileLayoutSource>();
connect(layoutSource.get(), &PhosphorLayout::ILayoutSource::contentsChanged,
        this, &Settings::refreshAlgorithmPickerModel);

// Render previews for every algorithm into the picker UI.
for (const auto& preview : layoutSource->availableLayouts()) {
    m_pickerModel->addPreview(preview);
}
```

The settings app binary does not link `plasmazones` or the daemon — only
`PhosphorTiles` and `PhosphorLayoutApi`. The picker preview thumbnails come
from the same `previewFromAlgorithm` pipeline the overlay uses at runtime.

---

## Threading Model

| Thread | What runs | Contract |
|--------|-----------|----------|
| GUI / main | All public API: registry access, `TilingState` mutation, `calculateZones`, `ScriptedAlgorithmLoader`, `AutotileLayoutSource` | Serialised to the thread that owns QCoreApplication. `AlgorithmRegistry::registerAlgorithm` asserts on this. |
| Watchdog | `ScriptedAlgorithmWatchdog::threadMain` | One OS thread process-wide. Only touches the watchdog mutex + `QJSEngine::setInterrupted` (documented thread-safe). Joined in the singleton destructor on shutdown. |

`QJSEngine` is inherently single-threaded — `ScriptedAlgorithm` documents
this explicitly despite the base-class `const` contract. All script
evaluation, all JS override resolution, and all tree/state-to-JS
conversion run on the main thread.

`TilingAlgorithm::calculateZones` is nominally `const` and
instance-reentrant for C++ algorithms (they only touch local state), but
scripted algorithms reuse a single engine and rely on the
`m_evaluating` atomic re-entrancy guard — the engine asserts that
`calculateZones` does not recurse into itself from a JS `onWindowAdded`
hook.

The split tree is not thread-safe — all access must be serialised by the
caller. In practice, the daemon serialises via the main event loop; the
settings app only reads via `AutotileLayoutSource`, which doesn't touch
any live `TilingState`.

---

## Testing Strategy

Tests live outside the library (in `tests/unit/phosphor-tiles/` when the
dedicated test target lands) and exercise the library directly via its
public headers — no daemon state, no D-Bus, no KWin.

```cpp
void test_algorithm_registry_replaces_with_correct_signal_order()
{
    auto* registry = PhosphorTiles::AlgorithmRegistry::instance();
    QSignalSpy registered  (registry, &AlgorithmRegistry::algorithmRegistered);
    QSignalSpy unregistered(registry, &AlgorithmRegistry::algorithmUnregistered);

    auto* first  = new MyAlgorithm;
    auto* second = new OtherAlgorithm;
    registry->registerAlgorithm(QStringLiteral("t"), first);
    registry->registerAlgorithm(QStringLiteral("t"), second);
    // Signal order: unregistered(t, replacing=true) → registered(t)
    QCOMPARE(unregistered.count(), 1);
    QCOMPARE(unregistered.at(0).at(1).toBool(), true);
    QCOMPARE(registry->algorithm(QStringLiteral("t")), second);
}

void test_split_tree_rebuild_preserves_ratios_positionally()
{
    PhosphorTiles::SplitTree tree;
    tree.insertAtEnd(QStringLiteral("a"));
    tree.insertAtEnd(QStringLiteral("b"));
    tree.resizeSplit(QStringLiteral("a"), 0.3);

    tree.rebuildFromOrder({QStringLiteral("b"), QStringLiteral("a")});
    // Positional preservation — the first split still has ratio 0.3.
    QCOMPARE(tree.root()->splitRatio, 0.3);
}

void test_scripted_sandbox_blocks_eval()
{
    PhosphorTiles::ScriptedAlgorithm algo(QFINDTESTDATA("fixtures/try_eval.js"));
    // Script calls eval("return 1+1"); should have returned an empty zones array
    // because calculateZones raises a ReferenceError.
    PhosphorTiles::TilingState state(QStringLiteral("s"));
    state.addWindow(QStringLiteral("w"));
    PhosphorTiles::TilingParams p;
    p.windowCount = 1;
    p.screenGeometry = QRect(0, 0, 100, 100);
    p.state = &state;
    QVERIFY(algo.calculateZones(p).isEmpty());
}

void test_watchdog_interrupts_infinite_loop()
{
    PhosphorTiles::ScriptedAlgorithm algo(QFINDTESTDATA("fixtures/infinite.js"));
    PhosphorTiles::TilingState state(QStringLiteral("s"));
    state.addWindow(QStringLiteral("w"));
    PhosphorTiles::TilingParams p;
    p.windowCount = 1;
    p.screenGeometry = QRect(0, 0, 100, 100);
    p.state = &state;

    QElapsedTimer t; t.start();
    QVERIFY(algo.calculateZones(p).isEmpty());
    // 100 ms deadline + some buffer for JIT warm-up
    QVERIFY(t.elapsed() < 500);
}
```

Coverage targets:
- `TilingAlgorithm::distribute*` helpers — rounding edge cases, pixel-
  perfect remainder distribution, minimum-size proportional fallback.
- `SplitTree` — insert at focused / end / position, removal collapsing,
  swap, resize, JSON round-trip (including adversarial inputs: depth
  overflow, node count overflow, duplicate ids, out-of-range ratios).
- `TilingState` — master / split clamping, rebuildSplitTree positional
  preservation, `toJson`/`fromJson` round-trip.
- `AlgorithmRegistry` — registration / replacement / unregistration,
  signal ordering, id validation, `aboutToQuit` cleanup.
- `ScriptedAlgorithm` — metadata parsing, sandbox hardening success and
  failure paths, builtin injection order, IIFE shadowing of `eval` /
  `Function`, watchdog arming / disarming.
- `AutotileLayoutSource` — cache invalidation on each of the three
  registry signals, FIFO eviction at cap, `previewAt` id parsing.

---

## Migration Path

Phase 1 — library lands, daemon still imports through the old headers:
- `libs/phosphor-tiles/` builds and installs alongside PlasmaZones.
- Old `src/autotile/` and `src/core/` headers forward to
  `PhosphorTiles::` via re-includes (`core/constants.h` already includes
  `AutotileConstants.h` for source compatibility).
- No behaviour change.

Phase 2 — consumer-by-consumer migration (already done in PR #334):
- Daemon includes `<PhosphorTiles/...>` directly instead of the forwarding
  shims.
- Editor and settings app gain `AutotileLayoutSource` consumption and
  drop their own preview-rendering code.
- KCM algorithm picker uses the library's `customParamDefList` for
  per-algorithm parameter UI.

Phase 3 — future `phosphor-tile-engine` extraction:
- Extract `AutotileEngine`, per-screen config resolver, and overflow
  policy from `src/autotile/` into a sibling library that links
  PhosphorTiles.
- PlasmaZones's daemon becomes a thin consumer of `PhosphorTileEngine`,
  which itself consumes `PhosphorTiles`. The two libraries can then be
  packaged separately.

---

## Rejected Alternatives

### Embedding the engine inside the library

Rejected: the engine is inherently daemon-coupled (window registry,
D-Bus adaptor, overflow settings, per-screen resolver, insert-position
policy). Bundling it would drag PlasmaZones types into the library and
make standalone consumption impossible. The clean split is
"algorithms + state + sandbox here, window placement there".

### One watchdog thread per scripted algorithm

Rejected: the previous design spawned a `std::thread` per live
`ScriptedAlgorithm`. At N registered scripts that's N permanently-running
threads even with no scripts executing. The shared-watchdog model uses
one thread total, tracks the earliest deadline across all armed entries,
and wakes on `condition_variable::wait_until`. Tested scalability up to
100 concurrent armed scripts with no observable scheduling overhead.

### Erasing the watchdog entry on disarm

Rejected: creates a dereference race with the watchdog thread's
`wait_until` predicate. The generation-counter pattern keeps the entry
alive and uses `deadline == time_point::max()` as a sentinel the
"find earliest deadline" loop ignores. Disarm is then just a generation
bump plus a sentinel write, both under the mutex — no races with the
thread, no need for per-entry locks.

### Capturing `now()` before the watchdog mutex

Rejected: a stall on lock contention could push the stored deadline
into the past, firing the watchdog immediately on a script that never
actually timed out. `arm()` computes the deadline inside the locked
section.

### Stitching PZ_* constants into JS source strings

Rejected: `QString::number(0.5)` is locale-dependent — under `de_DE` it
emits `"0,5"`, which a JS parser reads as a sequence-expression yielding
`5`. Using `QJSValue::setProperty` binds the native numeric value past
the parser entirely.

### Freezing builtin helpers in one batch

Rejected: freezing in one batch at the end leaves a window where an
already-injected helper can be shadowed by a later one via plain
`this.X = ...` assignment. The per-step freeze closes the gap so adding
a new builtin tomorrow cannot regress the invariant. The trusted builtin
set makes that a latent hazard today; the defense is in depth.

### Allowing `Proxy` through with a use-case warning

Rejected: `Proxy` is the single most powerful sandbox-escape primitive
in ES6+ — it intercepts every property access and can construct forged
objects that pass `typeof` / prototype checks. No algorithm author has
ever needed it, and the cost of allowing it is a complete sandbox
bypass one clever handler away.

### Blocking `WeakMap` and `WeakSet` for symmetry with `WeakRef`

Rejected: `WeakMap` / `WeakSet` have no exposed `.constructor` escape
once the built-in lockdown has run, and several legitimate algorithms
use them for leaf-identity tracking in tree structures. `WeakRef` and
`FinalizationRegistry` are different — they can observe GC behaviour
and enable side-channel attacks on the engine.

### Multi-registry composition inside a process

Rejected: every callsite (preview renderer, scripted-algorithm loader,
settings bridge) would need to know which of several registries owns the
id it wants to resolve. The singleton is the honest model: one catalog
per process, cross-process state transferred by host-application code.
Tests bypass the singleton via `AutotileLayoutSource(ownRegistry)` for
isolation.

### Lazy registry initialisation

Rejected: `AlgorithmRegistry::instance()` must be reachable from
static-init-time `AlgorithmRegistrar<T>` constructors, which precede
`QCoreApplication` construction. The current design queues those into
`pendingAlgorithmRegistrations()` (a plain `QList` at namespace scope)
and drains them from the singleton constructor, which is guaranteed to
run after `QCoreApplication::instance()` exists (`instance()` asserts
this). A lazy-init model with double-checked locking would pay a runtime
cost on every access without solving a real problem.

### QML `import PhosphorTiles 1.0`

Rejected: the library is a C++ library with no QML bindings. A QML
layer would couple it to `QQmlEngine` (which is already a PRIVATE
dep only for `QJSEngine`). Consumers that want algorithm previews in
QML go through `AutotileLayoutSource` — `LayoutPreview` is already
designed for QML consumption.

---

## Extensions (shipped in v1)

- **22 builtin JS helpers** in `src/builtins/*.js`, compiled into
  `:/builtins/` via `builtins.qrc`. Every helper is a port of a C++
  `TilingAlgorithm` protected static. Shipping them in-library (rather
  than asking authors to copy-paste) eliminates drift between
  scripted and C++ behaviour and keeps user scripts tiny —
  a master-stack variant can be 20 lines because 80% of the work is
  in the injected `masterStackLayout` helper.
- **`@param` custom parameter declarations** — scripts declare their own
  tunables via leading comment lines; the settings UI discovers them
  via `customParamDefList()` and renders number sliders / booleans /
  enum dropdowns automatically.
- **`id` override** — a scripted algorithm can claim a
  well-known id (e.g. `"master-stack"`) rather than `"script:<basename>"`,
  which is how PlasmaZones ships most "built-in" algorithms as JS
  files internally while preserving stable ids in user config.
- **Lifecycle hooks (v2)** — `onWindowAdded` / `onWindowRemoved` let
  scripts maintain incremental state (a split tree, a layout cache)
  instead of recomputing everything in `calculateZones`.
- **Hot reload** — `ScriptedAlgorithmLoader` watches the algorithm
  directories and swaps in updated scripts with debounced refresh. The
  signal order `algorithmUnregistered → algorithmRegistered` means
  downstream caches (including `AutotileLayoutSource`'s preview cache)
  rebuild cleanly.

---

## Directory Layout

```
libs/phosphor-tiles/
├── CMakeLists.txt
├── PhosphorTilesConfig.cmake.in
├── include/
│   └── PhosphorTiles/
│       ├── PhosphorTiles.h                  (umbrella include)
│       ├── AutotileConstants.h              (AutotileDefaults/JsonKeys/JsonValues)
│       ├── TilingAlgorithm.h                (base class)
│       ├── TilingAlgorithmHelpers.h         (ThreeColumnWidths, CumulativeMinDims)
│       ├── TilingParams.h                   (TilingParams, WindowInfo, ScreenInfo,
│       │                                     buildWindowInfos)
│       ├── TilingState.h
│       ├── SplitTree.h
│       ├── AlgorithmRegistry.h
│       ├── ScriptedAlgorithm.h
│       ├── ScriptedAlgorithmHelpers.h       (ScriptMetadata, CustomParamDef parser)
│       ├── ScriptedAlgorithmJsBuiltins.h    (22 string-returning loaders)
│       ├── ScriptedAlgorithmLoader.h        (discovery + hot reload)
│       ├── ScriptedAlgorithmSandbox.h       (hardenSandbox free function)
│       ├── AutotileLayoutSource.h           (ILayoutSource adapter)
│       └── AutotilePreviewRender.h          (previewFromAlgorithm free functions)
├── src/
│   ├── algorithmregistry.cpp
│   ├── autotilelayoutsource.cpp
│   ├── tilingalgorithm.cpp                  (distribute*, innerRect, solveThree*)
│   ├── tilingstate.cpp
│   ├── tilingstateserialization.cpp
│   ├── splittree.cpp                        (queries + mutations)
│   ├── splittreerebuild.cpp                 (rebuildFromOrder path)
│   ├── splittreeserializer.cpp              (toJson/fromJson)
│   ├── scriptedalgorithm.cpp                (loadScript + builtin injection)
│   ├── scriptedalgorithm_hooks.cpp          (lifecycle hook bridge)
│   ├── scriptedalgorithm_tree.cpp           (splitNodeToJSValue)
│   ├── scriptedalgorithmhelpers.cpp         (metadata parser, jsArrayToRects)
│   ├── scriptedalgorithmjsbuiltins.cpp      (resource loaders)
│   ├── scriptedalgorithmloader.cpp
│   ├── scriptedalgorithmsandbox.cpp         (hardenSandbox impl)
│   ├── scriptedalgorithmwatchdog.h          (internal — not installed)
│   ├── scriptedalgorithmwatchdog.cpp
│   ├── tileslogging.h                       (internal logging category)
│   ├── tileslogging.cpp
│   ├── builtins.qrc                         (:/builtins/ prefix)
│   └── builtins/
│       ├── applyTreeGeometry.js
│       ├── appendGracefulDegradation.js
│       ├── applyPerWindowMinSize.js
│       ├── clampSplitRatio.js
│       ├── computeCumulativeMinDims.js
│       ├── deckLayout.js
│       ├── distributeEvenly.js
│       ├── distributeWithGaps.js
│       ├── distributeWithMinSizes.js
│       ├── distributeWithOptionalMins.js
│       ├── dwindleLayout.js
│       ├── equalColumnsLayout.js
│       ├── extractMinDims.js
│       ├── extractRegionMaxMin.js
│       ├── fillArea.js
│       ├── fillRegion.js
│       ├── interleaveStacks.js
│       ├── lShapeLayout.js
│       ├── masterStackLayout.js
│       ├── solveThreeColumn.js
│       ├── solveTwoPart.js
│       └── threeColumnLayout.js
└── tests/                                   (pending — see Phase 3)
```

### CMake surface

```cmake
find_package(PhosphorTiles CONFIG REQUIRED)
target_link_libraries(mytarget PUBLIC PhosphorTiles::PhosphorTiles)
```

PUBLIC deps: `Qt6::Core`, `Qt6::Qml`, `PhosphorLayoutApi::PhosphorLayoutApi`.
The `Qt6::Qml` PUBLIC link is unfortunate but unavoidable — `ScriptedAlgorithm.h`
includes `<QJSValue>` and `<QJSEngine>` forward declarations are in
`Qt6::Qml`. Consumers that only touch C++ algorithms pay the link cost
but don't invoke any QML machinery at runtime.

`UNITY_BUILD OFF` is explicit in the CMakeLists: unity builds merge TUs
and surface spurious "defined but not used" warnings on anonymous-namespace
helpers that GCC can't fully track across the merge. The TUs are small
enough that unity buys nothing.

`CXX_VISIBILITY_PRESET hidden` + `VISIBILITY_INLINES_HIDDEN ON` means only
`PHOSPHORTILES_EXPORT`-marked symbols appear in the installed library's
public ABI. The internal `scriptedalgorithmwatchdog.h` has no export
macro and its header lives under `src/` so out-of-tree consumers cannot
include it.

---

## Open Questions

Resolved during this design pass:
- ✅ Singleton vs injected registry → singleton-per-process; tests inject
  their own instance through `AutotileLayoutSource`.
- ✅ Watchdog threading model → one shared thread with generation-counter
  disarm.
- ✅ Builtin-helper shipping model → 22 Qt resource `.js` files, injected
  + frozen per-step in topological order.
- ✅ Qt6::Qml public vs private → PRIVATE at link time for the
  implementation, but the PUBLIC header surface (`QJSValue`) forces
  PUBLIC in target_link_libraries.
- ✅ Standalone distribution → library is already self-contained (Qt6 +
  PhosphorLayoutApi only); PR #334 completed the carve-out.

Deferred to implementation follow-ups:
- Whether the watchdog should expose a per-instance timeout override for
  algorithms that legitimately need longer than 100 ms (e.g. a layout
  that solves an LP). The current workaround is to raise
  `ScriptWatchdogTimeoutMs` compile-time; a runtime setter would
  complicate the race analysis without a concrete user-demand signal.
- Whether to extract `AutotileEngine` into a sibling `phosphor-tile-engine`
  library now that `PhosphorTiles` is standalone. Current plan: wait
  until the daemon has one more consumer of the engine contract (a
  compositor-internal runtime would qualify) before making the split.
- Dedicated `tests/` directory inside `libs/phosphor-tiles/`. Tests
  currently live in `tests/unit/` under the PlasmaZones root and exercise
  the library through the daemon's shim headers; a library-local test
  target keeps test binaries small and proves the "standalone
  distribution" claim by building without any `src/` includes.
- ~~Whether `@builtinId` should be renamed~~ — done: renamed to `id` in the
  JS metadata API. The C++ struct field and accessor are also `id` now.
  not the user-facing one. Low priority; the existing name is load-bearing
  for the current shipped scripts.
- Whether to bump the public header count by splitting `SplitTree` into a
  pure-data `SplitNode` header + a behaviour header. Current shape (one
  header, one class + one POD struct) is pragmatic; only worth splitting
  if another library type ends up needing `SplitNode` without
  `SplitTree`.
