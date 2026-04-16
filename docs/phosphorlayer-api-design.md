# PhosphorLayer — API Design Document

## Overview

PhosphorLayer is a Qt6/C++20 library for managing the lifecycle of
wlr-layer-shell surfaces: overlays, panels, docks, backgrounds, modals, and
on-screen displays. It generalizes the per-screen surface management machinery
currently embedded in PlasmaZones' `OverlayService` (~7 distinct surface types,
~300 LOC of duplicated boilerplate) into a reusable library that any Wayland
application targeting KDE Plasma (or any wlr-layer-shell compositor) can
consume.

**License:** LGPL-2.1-or-later
**Namespace:** `PhosphorLayer`
**Depends on:** PhosphorShell (default `ILayerShellTransport` implementation),
Qt6 Core/Gui/Quick

---

## Dependency Graph

```
PhosphorShell              PhosphorLayer                PlasmaZones
(layer-shell QPA,          (surface lifecycle,          (OverlayService
 LayerSurface,              topology coord, presets,     becomes a thin
 compositor resilience)     role registry)               consumer)
        │                          │                            │
        └──── PUBLIC link ─────────┘                            │
                                   └──── PUBLIC link ───────────┘

Also usable without PlasmaZones: panel applets, notification daemons,
lockscreens, live wallpaper apps.
```

A panel application uses PhosphorLayer with its own content QML and its own
`IScreenProvider` backed by `QGuiApplication::screens()`. PlasmaZones injects
a virtual-screen-aware `IScreenProvider` instead.

---

## Design Principles

1. **No consumer-specific types** — no zones, no overlays, no PlasmaZones
   concepts in the public API. "Layer surface" is the only domain object.
2. **Dependency injection everywhere** — `IScreenProvider`,
   `ILayerShellTransport`, and `QQmlEngine` are all injectable. No hard-wired
   dependencies on `QGuiApplication::screens()` or PhosphorShell.
3. **Isolation over convenience** — each `Surface` owns its own `QQmlEngine`
   by default. Sharing is opt-in, explicit, and consumer-controlled. Hidden
   coupling through shared engines is rejected.
4. **Open presets, closed primitives** — `Role` is an open value type (any
   consumer can define one); `Layer`/`Anchor`/`KeyboardInteractivity` are
   closed enums (wlr-layer-shell protocol values).
5. **Single responsibility** — creation, registry bookkeeping, and topology
   response are separate classes. Each is independently testable.
6. **No globals** — every piece of state hangs off an instance the consumer
   owns. Clear lifetime, clear composition.
7. **Explicit lifecycle** — formal state machine on every `Surface`.
   "Did I already call show()?" is not a valid runtime question.
8. **Policy is the consumer's** — focus coordination, z-ordering between
   sibling surfaces, input routing: all consumer decisions. Library exposes
   primitives, not policy.

---

## Public API

### 1. Role — `PhosphorLayer::Role`

A value type describing a surface's protocol-level configuration: which
layer, which anchors, what exclusive zone, what keyboard interactivity.
Consumers pick from library-provided presets **or define their own**.

```cpp
#include <PhosphorLayer/Role>

namespace PhosphorLayer {

struct Role {
    Layer layer = Layer::Overlay;
    Anchors anchors = AnchorNone;
    int exclusiveZone = -1;                  // -1 = ignore other surfaces' zones
    KeyboardInteractivity keyboard = KeyboardInteractivity::None;
    QMargins defaultMargins;
    QString scopePrefix;                     // namespace for this role's surfaces

    // Fluent modifiers return a copy with the field changed — for defining
    // derivative roles without mutation. Enables `Roles::TopPanel.withExclusiveZone(30)`.
    [[nodiscard]] Role withLayer(Layer l) const;
    [[nodiscard]] Role withAnchors(Anchors a) const;
    [[nodiscard]] Role withExclusiveZone(int z) const;
    [[nodiscard]] Role withKeyboard(KeyboardInteractivity k) const;
    [[nodiscard]] Role withMargins(QMargins m) const;
    [[nodiscard]] Role withScopePrefix(QString prefix) const;
};

// ── Library-shipped presets (namespace, not enum — extensible) ────────
namespace Roles {
    inline constexpr Role FullscreenOverlay { Layer::Overlay, AnchorAll,
                                              -1, KeyboardInteractivity::None,
                                              {}, u"pl-fullscreen"_s };

    inline constexpr Role TopPanel { Layer::Top,
                                     AnchorTop | AnchorLeft | AnchorRight,
                                     0, KeyboardInteractivity::OnDemand,
                                     {}, u"pl-top-panel"_s };
    inline constexpr Role BottomPanel { /* mirror */ };
    inline constexpr Role LeftDock    { /* ... */ };
    inline constexpr Role RightDock   { /* ... */ };

    inline constexpr Role CenteredModal   { Layer::Top, AnchorNone,
                                            -1, KeyboardInteractivity::Exclusive,
                                            {}, u"pl-modal"_s };
    inline constexpr Role CornerToast     { /* ... */ };
    inline constexpr Role Background      { Layer::Background, AnchorAll,
                                            0, KeyboardInteractivity::None,
                                            {}, u"pl-background"_s };
    inline constexpr Role FloatingOverlay { /* ... */ };
}

} // namespace PhosphorLayer
```

Consumer-defined presets compose naturally:

```cpp
// In PlasmaZones
namespace PzRoles {
    inline constexpr Role Overlay =
        Roles::FullscreenOverlay.withScopePrefix(u"pz-overlay"_s);
    inline constexpr Role SnapAssist =
        Roles::CenteredModal.withScopePrefix(u"pz-snap-assist"_s);
    inline constexpr Role LayoutOsd =
        Roles::CornerToast.withMargins({0, 40, 0, 0})
                          .withScopePrefix(u"pz-layout-osd"_s);
}
```

---

### 2. SurfaceConfig — `PhosphorLayer::SurfaceConfig`

Immutable value type passed to `SurfaceFactory::create()`. Aggregates the
role with per-instance data (content, screen, overrides, context).

```cpp
struct SurfaceConfig {
    Role role;                              // REQUIRED — copied at construction
    QUrl contentUrl;                        // QML root (mutually exclusive with contentItem)
    std::unique_ptr<QQuickItem> contentItem;// Pre-built item (ownership transferred)
    QScreen* screen = nullptr;              // Nullable — resolved by affinity at creation
    QVariantMap contextProperties;          // Injected into the surface's QML context
    QQmlEngine* sharedEngine = nullptr;     // nullptr = per-surface engine (default, isolated)

    // Per-instance overrides (nullopt = use role's default)
    std::optional<Layer> layerOverride;
    std::optional<Anchors> anchorsOverride;
    std::optional<int> exclusiveZoneOverride;
    std::optional<KeyboardInteractivity> keyboardOverride;
    std::optional<QMargins> marginsOverride;

    // Observability
    QString debugName;                      // Logged in state transitions
};
```

SurfaceConfig is moved into the Surface at construction and stored `const`
internally. Reconfiguration = destroy + recreate — matches the wlr-layer-shell
protocol which forbids most post-show property changes.

---

### 3. Surface — `PhosphorLayer::Surface`

Represents one layer-shell surface. Owns its `QQuickWindow` and (by default)
its own `QQmlEngine`. Not constructible directly — only via `SurfaceFactory`
so the factory can inject dependencies.

```cpp
class PHOSPHORLAYER_EXPORT Surface : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)

public:
    enum class State {
        Constructed,   // SurfaceConfig accepted; no window yet
        Warming,       // QML compiling / engine initializing (hidden)
        Shown,         // Layer surface configured by compositor; visible
        Hiding,        // hide() called; waiting for compositor acknowledgement
        Hidden,        // Fully hidden; can transition back to Shown
        Recreating,    // Destroyed in response to topology change; will respawn
        Failed,        // Unrecoverable (QML error, transport rejected, etc.)
    };
    Q_ENUM(State)

    ~Surface() override;

    State state() const;
    const SurfaceConfig& config() const;    // Read-only view for consumers

    // Lifecycle transitions — idempotent, safe to call in any state.
    // Guarded: invalid transitions emit a warning and no-op.
    void show();
    void hide();
    void warmUp();                          // Pre-compile QML without showing

    // Escape hatches (use sparingly — prefer the declarative API above)
    QQuickWindow* window() const;
    PhosphorShell::LayerSurface* transport() const;

Q_SIGNALS:
    void stateChanged(State from, State to);
    void failed(const QString& reason);
    void aboutToRecreate();                 // Last chance to save content state

private:
    friend class SurfaceFactory;
    Surface(SurfaceConfig cfg, SurfaceDeps deps, QObject* parent);

    // State machine transitions enumerated in one place, not scattered
    // across 15 slots. See §6 for full transition table.
    class StateMachine;
    std::unique_ptr<StateMachine> m_sm;
};
```

#### State transition table

| From          | Event                    | To           | Notes                      |
|---------------|--------------------------|--------------|----------------------------|
| Constructed   | `warmUp()`               | Warming      | Creates engine + window, hidden |
| Constructed   | `show()`                 | Warming → Shown (chained) | |
| Warming       | (QML compiled)           | Hidden       | Via warmUp path            |
| Warming       | (QML error)              | Failed       | Emits `failed()`           |
| Hidden/Warming| `show()`                 | Shown        | Calls transport            |
| Shown         | `hide()`                 | Hiding       | Waits for compositor ack   |
| Hiding        | (transport confirmed)    | Hidden       |                            |
| *             | (screen removed)         | Recreating   | Factory destroys + respawns|
| Recreating    | (respawn complete)       | Shown/Hidden | Restores prior state       |
| *             | (transport rejected)     | Failed       |                            |

Invalid transitions (e.g., `show()` from `Failed`) log a `qCWarning` with the
debugName and no-op. They do not throw.

---

### 4. SurfaceFactory — `PhosphorLayer::SurfaceFactory`

Stateless constructor (holds references to injected dependencies only). One
responsibility: turn a `SurfaceConfig` into a live `Surface`.

```cpp
class PHOSPHORLAYER_EXPORT SurfaceFactory : public QObject {
    Q_OBJECT
public:
    struct Deps {
        ILayerShellTransport* transport = nullptr;  // REQUIRED
        IQmlEngineProvider* engineProvider = nullptr; // Optional — default: per-surface
        IScreenProvider* screens = nullptr;         // REQUIRED
        QString loggingCategory = u"phosphorlayer"_s;
    };
    explicit SurfaceFactory(Deps deps, QObject* parent = nullptr);

    // Creates exactly one Surface bound to the given screen (or config.screen
    // if non-null). For multi-screen patterns, use ScreenSurfaceRegistry.
    // Returns nullptr and logs if deps.transport->isSupported() is false.
    [[nodiscard]] Surface* create(SurfaceConfig cfg, QObject* parent = nullptr);
};
```

Failure modes (all yield `nullptr` + logged reason):
- Transport unsupported (compositor lacks wlr-layer-shell)
- Invalid config (both `contentUrl` and `contentItem` set)
- Screen required but `config.screen` nullptr and no primary from provider

---

### 5. ScreenSurfaceRegistry — `PhosphorLayer::ScreenSurfaceRegistry`

Per-screen surface bookkeeping for affinities that imply multiple surfaces
(`AllScreens`). Generic on the surface type so consumers that subclass
`Surface` keep their type through the API.

```cpp
template <typename SurfaceT = Surface>
class ScreenSurfaceRegistry {
public:
    ScreenSurfaceRegistry(SurfaceFactory* factory, IScreenProvider* screens);

    // Create one SurfaceT per screen reported by the provider.
    // Returns the current set. Safe to call repeatedly — idempotent against
    // the provider's current screen list.
    std::vector<SurfaceT*> createForAllScreens(SurfaceConfig tpl);

    // Diff-based sync: destroys surfaces for removed screens, creates surfaces
    // for added screens. Wired up by TopologyCoordinator on `screensChanged`.
    void syncToScreens();

    SurfaceT* surfaceForScreen(QScreen* s) const;
    std::vector<SurfaceT*> surfaces() const;
    void clear();                           // Destroys all surfaces
};
```

---

### 6. TopologyCoordinator — `PhosphorLayer::TopologyCoordinator`

Reacts to screen hot-plug / virtual-screen reconfiguration / compositor
restart. Drives recreations via the registry.

```cpp
class PHOSPHORLAYER_EXPORT TopologyCoordinator : public QObject {
    Q_OBJECT
public:
    struct Config {
        int debounceMs = 200;               // Qt emits screensChanged repeatedly
        bool debugLogDiffs = false;
    };
    TopologyCoordinator(IScreenProvider* screens,
                        ILayerShellTransport* transport,
                        Config cfg = {},
                        QObject* parent = nullptr);

    // Attach a registry — the coordinator will keep its screen set in sync.
    // Multiple registries may be attached (e.g., overlays + panels).
    template <typename SurfaceT>
    void attach(ScreenSurfaceRegistry<SurfaceT>* reg);
    void detach(auto* reg);

Q_SIGNALS:
    void screensChanging();                 // Debounce fired; recreation imminent
    void screensChanged();                  // All registries synced
    void compositorRestarted();             // Transport signalled global removal
};
```

---

## Injected Interfaces

### IScreenProvider

```cpp
class PHOSPHORLAYER_EXPORT IScreenProvider {
public:
    virtual ~IScreenProvider() = default;
    virtual QList<QScreen*> screens() const = 0;
    virtual QScreen* primary() const = 0;
    virtual QScreen* focused() const = 0;   // Screen containing cursor / last-focused
    virtual QObject* notifier() = 0;        // emits void changed()
};

// Default implementation — backed by QGuiApplication
class PHOSPHORLAYER_EXPORT DefaultScreenProvider : public QObject, public IScreenProvider { /* ... */ };
```

PlasmaZones injects a virtual-screen-aware provider; standalone consumers use
`DefaultScreenProvider`.

### ILayerShellTransport

Abstracts the protocol binding so tests don't need Wayland.

```cpp
class PHOSPHORLAYER_EXPORT ILayerShellTransport {
public:
    virtual ~ILayerShellTransport() = default;
    virtual bool isSupported() const = 0;
    // Attach a QQuickWindow to the layer-shell protocol with initial config.
    // Returns a handle the surface uses to apply mutable properties later.
    virtual std::unique_ptr<ITransportHandle> attach(QQuickWindow* win,
                                                     const TransportAttachArgs& args) = 0;
    // Global-removed callback (compositor restart detection)
    virtual void addCompositorLostCallback(std::function<void()> cb) = 0;
};

// Default implementation — wraps PhosphorShell::LayerSurface
class PHOSPHORLAYER_EXPORT PhosphorShellTransport : public ILayerShellTransport { /* ... */ };
```

### IQmlEngineProvider (optional)

```cpp
class PHOSPHORLAYER_EXPORT IQmlEngineProvider {
public:
    virtual ~IQmlEngineProvider() = default;
    // Called once per Surface construction. Default impl returns a new engine;
    // consumers that want engine sharing return the same engine repeatedly.
    virtual QQmlEngine* engineForSurface(const SurfaceConfig& cfg) = 0;
    // Called when a surface using the returned engine is destroyed.
    // Default impl deletes the engine; sharing impls no-op.
    virtual void releaseEngine(QQmlEngine* engine) = 0;
};
```

---

## Composition Example (what a consumer writes)

```cpp
// Once at application startup — composition root
auto screens   = std::make_unique<DefaultScreenProvider>();
auto transport = std::make_unique<PhosphorShellTransport>();
auto factory   = std::make_unique<SurfaceFactory>(SurfaceFactory::Deps{
    .transport = transport.get(),
    .screens = screens.get(),
});
auto topology  = std::make_unique<TopologyCoordinator>(screens.get(),
                                                       transport.get());

// One surface per screen for a notification daemon
auto notifReg = std::make_unique<ScreenSurfaceRegistry<>>(factory.get(),
                                                          screens.get());
topology->attach(notifReg.get());
notifReg->createForAllScreens({
    .role = Roles::CornerToast.withMargins({0, 40, 40, 0}),
    .contentUrl = QUrl(u"qrc:/MyApp/NotificationItem.qml"_s),
    .contextProperties = {{u"notificationController"_s,
                           QVariant::fromValue(m_ctrl)}},
});

// Singleton modal
auto modal = factory->create({
    .role = Roles::CenteredModal,
    .contentUrl = QUrl(u"qrc:/MyApp/SettingsDialog.qml"_s),
    .debugName = u"settings-modal"_s,
});
modal->show();
```

---

## Threading Model

All public API: **GUI thread only.** Internals handle Wayland-event-loop
interactions via `QObject::connect` with Qt::QueuedConnection where needed.
Consumers never cross thread boundaries to use the library.

The only exception: `TopologyCoordinator::compositorRestarted()` may be
emitted from the Wayland event dispatch context; it uses `QueuedConnection`
by default so consumer slots run on the GUI thread.

---

## Testing Strategy

All dependencies are injectable → unit tests run headless without Wayland.

```cpp
class MockTransport : public ILayerShellTransport { /* ... */ };
class MockScreenProvider : public IScreenProvider { /* ... */ };

void test_surface_state_machine() {
    MockTransport t; MockScreenProvider s;
    SurfaceFactory f({&t, nullptr, &s});
    auto* surface = f.create({.role = Roles::CenteredModal,
                              .contentItem = std::make_unique<QQuickItem>()});
    QCOMPARE(surface->state(), Surface::State::Constructed);
    surface->show();
    QCOMPARE(surface->state(), Surface::State::Shown);
    t.simulateCompositorLoss();
    QCOMPARE(surface->state(), Surface::State::Recreating);
}
```

Coverage targets:
- State machine: every transition, including invalid ones (must not crash)
- Registry diffing: add/remove/reorder screens
- Topology debouncing: rapid `screensChanged` coalesces to one recreation cycle
- Compositor-lost: all registries respawn cleanly
- Role composition: `with*()` modifiers don't alias / share state

---

## Migration Path: PlasmaZones OverlayService

Phase 1 — library lands (no PZ change):
- `phosphorlayer/` builds and exports targets
- Unit tests pass

Phase 2 — one-at-a-time consumer migration:
1. `ShaderPreview` (simplest — singleton floating overlay) →
   `factory.create({.role = Roles::FloatingOverlay, ...})`
2. `LayoutPicker` and `SnapAssist` (singleton modals)
3. `LayoutOsd` and `NavigationOsd` (per-screen pre-warmed)
4. `ZoneSelector` (per-screen, keyboard-interactive)
5. `MainOverlay` (most complex — virtual-screen-aware, per-screen, shader rendering)

Each migration removes ~50–150 LOC from `overlayservice/*.cpp` and replaces
it with a `SurfaceConfig` + `Role` composition. At completion, `OverlayService`
keeps its domain logic (zone highlighting, snap assist content model) but all
layer-shell machinery is gone.

Phase 3 — PZ-specific `IScreenProvider` implementation:
- PlasmaZones' virtual-screen subdivision plugs in via a
  `VirtualScreenProvider : IScreenProvider` that exposes its v-screens as
  QScreen objects (or a parallel abstraction if that proves unclean).

---

## Rejected Alternatives (with rationale)

### Closed `enum class Role`
Rejected: violates open/closed principle. Consumers would hard-code
library-defined values and re-specify the same override combinations at every
call site. Open value-type presets compose naturally and let apps build their
own vocabulary.

### Pooled `QQmlEngine` at SurfaceManager scope
Rejected: hidden coupling. One surface's QML error or type registration
becomes visible to sibling surfaces. Explicit sharing via `SurfaceConfig::
sharedEngine` and `IQmlEngineProvider` is better: consumers who want pooling
opt in; the default is isolation.

Qt 6 shares parsed `QQmlCompilationUnit` across engines in the same process
via a global type cache, so the memory argument for pooling is much weaker
than it appears.

### Single monolithic `SurfaceManager`
Rejected: SRP violation. Creation, registry, and topology response have
different reasons to change and different test setups. Splitting them into
`SurfaceFactory` + `ScreenSurfaceRegistry` + `TopologyCoordinator` makes
each piece usable independently (a singleton modal doesn't need topology
handling at all).

### Library-managed focus coordination
Rejected: policy-in-library anti-pattern. "Exclusive surface A should demote
other surfaces to OnDemand" is one application's policy, not all of them.
Consumers call `show()`/`hide()` explicitly; the library exposes
`KeyboardInteractivity` as a primitive.

### Hard-wired `QGuiApplication::screens()` / `PhosphorShell::LayerSurface`
Rejected: untestable and inflexible. Injected `IScreenProvider` +
`ILayerShellTransport` make headless tests trivial and let PlasmaZones plug
in virtual-screen support without the library knowing.

### Mutable `SurfaceConfig` on live surfaces
Rejected: wlr-layer-shell doesn't support most post-show property changes
(layer, anchors, output, scope, keyboard all immutable per the protocol).
Pretending otherwise would hide the recreation that's actually happening.
Destroy + recreate is the honest model.

---

## Future Extensions

Not in scope for v1, but the architecture leaves room for:

- **Session restore**: serialize `SurfaceConfig` set to JSON; restore on launch.
  `ISurfaceStore` interface would slot in next to the other injected deps.
- **Multi-transport**: an `XdgToplevelTransport` for platforms without
  layer-shell — satisfies the `ILayerShellTransport` contract with reasonable
  degradation (no exclusive zones, position hints only).
- **Animations**: `ISurfaceAnimator` for fade/slide-in transitions. Currently
  left to the consumer's QML.
- **Accessibility**: a11y tree propagation across surfaces — requires AT-SPI
  wiring that PhosphorShell doesn't expose yet.

---

## Directory Layout

```
phosphorlayer/
├── CMakeLists.txt
├── PhosphorLayerConfig.cmake.in
├── include/
│   └── PhosphorLayer/
│       ├── PhosphorLayer.h        (umbrella header)
│       ├── Role                   (forwarding)
│       ├── Role.h
│       ├── SurfaceConfig          (forwarding)
│       ├── SurfaceConfig.h
│       ├── Surface                (forwarding)
│       ├── Surface.h
│       ├── SurfaceFactory         (forwarding)
│       ├── SurfaceFactory.h
│       ├── ScreenSurfaceRegistry  (forwarding)
│       ├── ScreenSurfaceRegistry.h
│       ├── TopologyCoordinator    (forwarding)
│       ├── TopologyCoordinator.h
│       ├── IScreenProvider.h
│       ├── ILayerShellTransport.h
│       ├── IQmlEngineProvider.h
│       └── defaults/
│           ├── DefaultScreenProvider.h
│           └── PhosphorShellTransport.h
├── src/
│   ├── role.cpp
│   ├── surface.cpp
│   ├── surfacestatemachine.cpp
│   ├── surfacefactory.cpp
│   ├── topologycoordinator.cpp
│   ├── defaults/
│   │   ├── defaultscreenprovider.cpp
│   │   └── phosphorshelltransport.cpp
│   └── internal.h
└── tests/
    ├── CMakeLists.txt
    ├── mocks/
    │   ├── mocktransport.h
    │   └── mockscreenprovider.h
    ├── test_role.cpp
    ├── test_surface_state_machine.cpp
    ├── test_factory.cpp
    ├── test_registry.cpp
    └── test_topology.cpp
```

---

## Open Questions

Resolved during this design pass:
- ✅ Pool vs per-surface engines → per-surface default, opt-in sharing
- ✅ Enum vs value-type Role → value type with presets namespace
- ✅ Focus coordination → consumer-owned
- ✅ Single manager vs split → split (Factory/Registry/Coordinator)
- ✅ Globals → none; everything hangs off injected instances

Deferred to implementation:
- How `IScreenProvider::focused()` is computed — likely consumer-defined.
  Default impl may return `primary()` with a note that "focused" requires
  compositor-specific hints.
- Exact debounce strategy for `screensChanged` — start with 200 ms timer;
  tune based on real hot-plug traces.
- Whether `ScreenSurfaceRegistry` should be header-only (template) or
  compiled via type erasure — probably keep the template in the header,
  provide an `-impl.h` for heavy paths.
