# PhosphorSurfaces -- API Design Document

## Overview

PhosphorSurfaces is a managed Wayland layer-shell surface lifecycle
library for Phosphor-based shells, compositors, and window managers. It
handles QML surface creation, warm-up, show/hide, screen hotplug, and
orderly teardown -- while remaining agnostic to the content displayed in
those surfaces.

In a shell ecosystem, surface lifecycle management is **infrastructure**.
Overlays, panels, OSDs, notification popups, desktop effects, and
third-party plugins all need the same machinery: create a layer-shell
surface on the right output, load QML into it, show/hide it efficiently,
handle screen hotplug, and avoid the Wayland+Vulkan teardown pitfalls
that make naive approaches unreliable. PhosphorSurfaces provides that
machinery once.

The library does **not** own QML content, zone-specific rendering types,
or application domain logic. Those are injected by the caller via an
engine configurator callback and per-surface QML URLs.

**License:** LGPL-2.1-or-later
**Namespace:** `PhosphorSurfaces`
**Depends on:** Qt6::Core, Qt6::Quick, PhosphorLayer::PhosphorLayer
**Build artefact:** `libPhosphorSurfaces.so` (SHARED)

---

## Dependency Graph

```
PhosphorLayer (layer-shell protocol, surface factory, transport)
       |
PhosphorSurfaces (surface lifecycle, hotplug, keep-alive, engine management)
       |
       +------ daemon (OverlayService, ZoneShaderItem, zone QML)
       +------ panel widget [future]
       +------ lock screen effects [future]
       +------ third-party plugins [future]
```

Every consumer constructs a `SurfaceManager`, passes an engine
configurator to install its domain types, and calls high-level surface
lifecycle methods. The library handles Wayland/Vulkan concerns internally.

---

## Design Principles

1. **Content-agnostic.** The library creates surfaces and loads QML URLs
   the caller provides. It never references zones, shaders, layouts, or
   any PlasmaZones domain type. Application-specific QML types are
   registered via the engine configurator.

2. **Engine configurator, not engine owner.** The library creates and
   owns the QQmlEngine internally, but calls a user-supplied
   `std::function<void(QQmlEngine&)>` once during initialization. The
   caller uses this callback to register types (`qmlRegisterType`),
   install i18n (`setContextObject`), and set context properties. The
   library never inspects what was installed.

3. **Virtual-screen-aware.** Surfaces are keyed by effective screen ID,
   not physical QScreen pointer. A single physical monitor may host
   multiple virtual screens, each with its own overlay. The caller
   provides the mapping; the library manages per-screen state.

4. **Vulkan-safe lifecycle.** On Wayland with Vulkan rendering, hiding a
   QQuickWindow destroys the wl_surface but Qt does not properly
   reinitialize VkSwapchainKHR on re-show. The library uses
   destroy-on-hide + create-on-show for affected surface types, and
   maintains a 1x1 keep-alive surface to prevent Qt from tearing down
   global Vulkan/Wayland protocol objects between show cycles.

5. **Warm-up support.** QML compilation on first load takes 100-300ms.
   The library supports pre-warming surfaces at startup (load QML, create
   window, keep hidden) so the first show is latency-free. Pre-warmed
   surfaces are reused across show/hide cycles without destruction.

6. **Screen hotplug as a first-class concern.** Screen add/remove
   triggers automatic surface creation/destruction for affected screens.
   The library handles the bookkeeping; the caller receives callbacks to
   push content into newly created surfaces.

---

## Public API

### `SurfaceManagerConfig`

```cpp
namespace PhosphorSurfaces {

struct SurfaceManagerConfig
{
    PhosphorLayer::SurfaceFactory* surfaceFactory;

    // Called once during construction. Caller registers QML types,
    // installs i18n context, and sets context properties here.
    std::function<void(QQmlEngine&)> engineConfigurator;

    // Optional: pipeline cache path for persisting shader compilation.
    // Empty string disables caching.
    QString pipelineCachePath;
};

}
```

### `SurfaceHandle`

```cpp
namespace PhosphorSurfaces {

// Opaque handle to a managed surface. Callers use this to push
// content updates, query state, and request show/hide.
class PHOSPHORSURFACES_EXPORT SurfaceHandle
{
public:
    QQuickWindow* window() const;
    bool isVisible() const;
    bool isWarmedUp() const;

    // Access the layer-shell transport for post-creation mutations
    // (margins, keyboard interactivity). Returns nullptr if the
    // compositor recycled the surface.
    PhosphorLayer::ITransportHandle* transport() const;

    // The effective screen ID this surface is bound to.
    QString screenId() const;
};

}
```

### `SurfaceSpec`

```cpp
namespace PhosphorSurfaces {

// Describes a surface to create. The caller builds these; the
// library creates the underlying PhosphorLayer::Surface.
struct SurfaceSpec
{
    QUrl qmlUrl;
    PhosphorLayer::Role role;
    QString debugName;

    // If set, override the role's default anchors/margins.
    // Used for virtual-screen positioning within a physical output.
    std::optional<PhosphorLayer::Anchors> anchorsOverride;
    std::optional<QMargins> marginsOverride;

    // Dynamic QML properties to set on the QQuickWindow root object
    // before the first frame renders.
    QVariantMap initialProperties;
};

}
```

### `ScreenMapping`

```cpp
namespace PhosphorSurfaces {

// Caller-provided mapping from effective screen IDs to physical
// QScreen pointers. The library does not compute virtual screens
// itself -- that is the caller's domain (e.g., ScreenManager).
struct ScreenTarget
{
    QString effectiveScreenId;
    QScreen* physicalScreen;
    QRect geometry;  // Effective geometry (may be subset of physical)
};

}
```

### `SurfaceManager`

```cpp
namespace PhosphorSurfaces {

class PHOSPHORSURFACES_EXPORT SurfaceManager : public QObject
{
    Q_OBJECT

public:
    explicit SurfaceManager(const SurfaceManagerConfig& config,
                            QObject* parent = nullptr);
    ~SurfaceManager() override;

    // ── Engine access ───────────────────────────────────────────
    // Read-only access for callers that need to set additional
    // context properties after construction (e.g., per-show updates).
    QQmlEngine* engine() const;

    // ── Surface lifecycle ───────────────────────────────────────

    // Create a surface on the given screen. Returns a handle for
    // subsequent operations. The surface is warmed up (QML loaded)
    // before returning. Returns nullptr on failure.
    SurfaceHandle* createSurface(const SurfaceSpec& spec,
                                 const ScreenTarget& screen);

    // Destroy a surface immediately. Cleans up Wayland resources.
    // The handle is invalid after this call.
    void destroySurface(SurfaceHandle* handle);

    // Destroy all surfaces bound to the given effective screen ID.
    void destroySurfacesForScreen(const QString& effectiveScreenId);

    // ── Show / hide ─────────────────────────────────────────────

    void showSurface(SurfaceHandle* handle);
    void hideSurface(SurfaceHandle* handle);

    // ── Warm-up (pre-create without showing) ────────────────────

    // Create and warm up a surface but don't show it. Useful for
    // OSD-style surfaces where first-show latency matters.
    SurfaceHandle* warmUpSurface(const SurfaceSpec& spec,
                                 const ScreenTarget& screen);

    // ── Screen hotplug ──────────────────────────────────────────

    // Notify the manager that screens have changed. The manager
    // destroys surfaces for removed screens and emits
    // screenAdded() so the caller can create surfaces for new ones.
    void updateScreens(const QList<ScreenTarget>& currentScreens);

    // ── Scope generation ────────────────────────────────────────

    // Returns a unique scope suffix for layer-shell surface scopes.
    // Prevents compositor rate-limiting on rapid destroy/recreate.
    quint64 nextScopeGeneration();

    // ── Keep-alive ──────────────────────────────────────────────

    // The keep-alive surface is created automatically during
    // construction and destroyed during destruction. No caller
    // action required. Exposed only for diagnostics.
    bool keepAliveActive() const;

Q_SIGNALS:
    // Emitted when a new physical screen is detected during
    // updateScreens(). The caller should create surfaces for it.
    void screenAdded(QScreen* screen);

    // Emitted when a screen is removed. All surfaces for this
    // screen have already been destroyed by the time this fires.
    void screenRemoved(const QString& effectiveScreenId);
};

}  // namespace PhosphorSurfaces
```

---

## What Stays in PlasmaZones

The following are **not** part of the library. They remain in the
daemon (GPL) because they encode PlasmaZones domain knowledge:

### Zone shader rendering (`src/daemon/rendering/`)

| File | Reason |
|---|---|
| `ZoneShaderItem` | Subclass of `PhosphorRendering::ShaderEffect` with zone-specific UBO layout matching `common.glsl`. Other consumers would write their own subclass. |
| `ZoneShaderNodeRhi` | Zone-specific QSGRenderNode. Tightly coupled to `ZoneShaderUniforms`. |
| `ZoneUniformExtension` | Writes zone rect/color/params arrays into the UBO. Binary layout matches GLSL. |
| `ZoneLabelTextureBuilder` | Pre-renders zone number labels. Domain-specific. |
| `ZoneShaderUniforms` | UBO struct with `BaseUniforms` + zone extension. GLSL-coupled. |

The reusable rendering primitive is `PhosphorRendering::ShaderEffect`
(already LGPL). `ZoneShaderItem` is a leaf-node specialization.

### QML overlay content (`src/ui/`)

| File | C++ coupling | Why it stays |
|---|---|---|
| `ZoneOverlay.qml` | None | Zone-specific data bindings |
| `RenderNodeOverlay.qml` | `ZoneShaderItem` | Zone shader rendering |
| `ZoneSelectorWindow.qml` | None | Zone selection UX |
| `SnapAssistOverlay.qml` | None | Snap-assist candidate grid |
| `LayoutPickerOverlay.qml` | None | Layout card grid |
| `LayoutOsd.qml` | None | Layout switch notification |
| `NavigationOsd.qml` | None | Keyboard nav feedback |
| `ZoneItem.qml` | None | Zone rectangle component |
| `ZoneLabel.qml` | None | Zone number label component |
| `ZoneSelector.qml` | None | Zone selection component |
| `LayoutPreview.qml` | None | Layout preview component |

10 of 11 files have zero C++ dependencies. All reference zone-specific
data shapes (zone IDs, highlight states, layout categories). A panel
widget or lock screen effect would write its own QML -- not reuse these.

### Shared QML components (`src/shared/qml/`)

`ZonePreview`, `LayoutCard`, `PopupFrame`, `CategoryBadge` are pure
Kirigami QML. `ZoneShaderRenderer` wraps `ZoneShaderItem`. These serve
the PlasmaZones UI -- they're app components, not library primitives.

### Orchestration logic in OverlayService

`OverlayService` retains the **policy** layer that decides _when_ and
_what_ to show. PhosphorSurfaces provides the _how_:

| OverlayService concern | Library provides |
|---|---|
| "Show zone overlay with these zones on these screens" | `createSurface()` + `showSurface()` |
| "Zones changed, update overlay windows" | `SurfaceHandle::window()` for property writes |
| "Shader or standard overlay for this screen?" | Nothing -- caller picks the QML URL |
| "Pre-warm OSDs at daemon startup" | `warmUpSurface()` |
| "Virtual screen geometry changed" | `destroySurfacesForScreen()` + `createSurface()` |
| "Screen hotplug" | `updateScreens()` + `screenAdded` signal |
| "Idle/un-idle per-VS overlay" | `SurfaceHandle::window()` for transparency toggle |
| "Rekey overlay from one VS ID to another" | `destroySurfacesForScreen()` + `createSurface()` |

---

## Integration Pattern

### Daemon (OverlayService)

```cpp
// Construction:
m_surfaceManager = std::make_unique<PhosphorSurfaces::SurfaceManager>(
    PhosphorSurfaces::SurfaceManagerConfig{
        .surfaceFactory = m_surfaceFactory.get(),
        .engineConfigurator = [this](QQmlEngine& engine) {
            auto* i18n = new PzLocalizedContext(&engine);
            engine.rootContext()->setContextObject(i18n);
            qmlRegisterType<ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");
        },
        .pipelineCachePath = pipelineCachePath(),
    });

connect(m_surfaceManager.get(), &PhosphorSurfaces::SurfaceManager::screenAdded,
        this, &OverlayService::onScreenAdded);

// Creating a zone overlay:
PhosphorSurfaces::SurfaceSpec spec{
    .qmlUrl = useShader ? qrcShaderOverlay : qrcStandardOverlay,
    .role = PzRoles::Overlay.withScopePrefix(
        QStringLiteral("plasmazones-overlay-%1-%2")
            .arg(screenId)
            .arg(m_surfaceManager->nextScopeGeneration())),
    .debugName = "overlay",
};

auto* handle = m_surfaceManager->createSurface(spec, screenTarget);
if (handle) {
    pushZoneDataToWindow(handle->window());
    m_surfaceManager->showSurface(handle);
}

// Pre-warming an OSD:
auto* osdHandle = m_surfaceManager->warmUpSurface(osdSpec, screenTarget);
// Later, on layout switch:
pushOsdDataToWindow(osdHandle->window());
m_surfaceManager->showSurface(osdHandle);
```

### Future: Panel Widget

```cpp
auto manager = std::make_unique<PhosphorSurfaces::SurfaceManager>(
    PhosphorSurfaces::SurfaceManagerConfig{
        .surfaceFactory = panelSurfaceFactory,
        .engineConfigurator = [](QQmlEngine& engine) {
            qmlRegisterType<PanelVisualizerItem>("PanelVis", 1, 0, "VisualizerItem");
            engine.rootContext()->setContextProperty("audioProvider", audioProvider);
        },
    });

// Panel creates its own surfaces with its own QML and its own types.
// Zero PlasmaZones coupling.
```

---

## Vulkan Keep-Alive Surface

### Problem

When all visible QQuickWindows are destroyed on Wayland with Vulkan
rendering, Qt tears down global protocol objects (`zwp_linux_dmabuf_v1`,
`wp_presentation`, `VkInstance`, `VkDevice`). The next surface creation
fails because `VkSwapchainKHR` cannot be created without a global
`VkInstance`.

### Solution

`SurfaceManager` creates a persistent 1x1 background-layer surface
during construction. This surface:

- Is invisible to the user (background layer, 1x1 size, no content)
- Keeps Qt's Vulkan/Wayland globals alive between show cycles
- Is destroyed last during `~SurfaceManager()`, after all other surfaces

The keep-alive is an internal implementation detail. Callers never
interact with it.

### Destruction Order

```
1. All caller-created surfaces (via destroySurface or ~SurfaceManager)
2. Deferred-delete drain (processEvents loop, 4-8 passes typical)
3. Keep-alive surface (final)
```

This ensures the Vulkan instance outlives all swapchain teardown.

---

## Scope Generation

Wayland compositors may rate-limit `configure` events per layer-shell
surface scope. Rapid destroy/recreate cycles (e.g., switching virtual
screens during a drag) can trigger this, causing surfaces to appear at
stale positions.

`SurfaceManager::nextScopeGeneration()` returns a monotonically
increasing `quint64`. Callers append it to their scope strings:

```cpp
auto role = baseRole.withScopePrefix(
    QStringLiteral("myapp-surface-%1-%2").arg(screenId).arg(mgr->nextScopeGeneration()));
```

Each surface gets a unique scope, resetting the compositor's rate-limit
counter.

---

## Screen Hotplug Protocol

```
updateScreens(currentScreens)
    |
    +-- For each screen no longer in list:
    |       destroySurfacesForScreen(removedId)
    |       emit screenRemoved(removedId)
    |
    +-- For each screen newly in list:
            emit screenAdded(newScreen)
            // Caller creates surfaces in response
```

The caller drives the screen mapping. The library does not enumerate
screens or compute virtual screen layouts -- it accepts `ScreenTarget`
structs that the caller builds from its own `ScreenManager` (or
equivalent).

---

## Migration from OverlayService

| Before (OverlayService internal) | After (PhosphorSurfaces) |
|---|---|
| `createLayerSurface()` private method | `SurfaceManager::createSurface()` |
| `m_engine` owned directly | Engine owned by `SurfaceManager`, configured via callback |
| `m_keepAliveSurface` managed manually | Automatic in `SurfaceManager` constructor/destructor |
| `m_scopeGeneration++` inline | `SurfaceManager::nextScopeGeneration()` |
| `handleScreenAdded()` / `handleScreenRemoved()` | `updateScreens()` + signals |
| `warmUpLayoutOsd()` / `warmUpNavigationOsd()` | `warmUpSurface()` |
| `PerScreenOverlayState` internal struct | `SurfaceHandle` per surface (caller groups by screen) |
| `m_screenStates` QHash | Internal to `SurfaceManager` (opaque) |

---

## Testing

Unit tests (`libs/phosphor-surfaces/tests/`):

- **Construction** -- manager creates keep-alive, engine configurator called once
- **Surface lifecycle** -- create, warm-up, show, hide, destroy
- **Screen hotplug** -- add/remove screens, signals emitted, surfaces cleaned up
- **Scope generation** -- monotonically increasing, unique per call
- **Destruction order** -- keep-alive outlives all surfaces

Integration tests (require Wayland compositor):

- Surface actually appears on correct output
- Vulkan rendering survives show/hide/show cycle
- Screen unplug during visible overlay does not crash

---

## Estimated Scope

| Component | LOC (est.) |
|---|---|
| `SurfaceManager` (engine, keep-alive, hotplug) | ~400 |
| `SurfaceHandle` (window access, transport, state) | ~100 |
| `SurfaceSpec` / `ScreenTarget` / config structs | ~60 |
| CMakeLists.txt + package config | ~140 |
| Unit tests | ~200 |
| **Total** | **~900** |

The extraction is small because the library provides **lifecycle
mechanics**, not business logic. The complexity lives in the caller's
orchestration policy (what to show, when, with what data) -- which stays
in the application.
