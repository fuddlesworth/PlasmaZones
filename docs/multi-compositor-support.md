# Multi-Compositor Support: Dropping the KDE LayerShellQt Dependency

## Overview

PlasmaZones currently depends on `LayerShellQt` (a KDE library) for all overlay
windows — zone overlays, OSDs, snap assist, geometry sensors. This ties the
daemon to KDE Plasma. Replacing LayerShellQt with a direct implementation of the
`zwlr_layer_shell_v1` Wayland protocol enables PlasmaZones to run on **any**
Wayland compositor without pulling in KDE Frameworks.

## Motivation

| Compositor | With LayerShellQt | With direct protocol |
|---|---|---|
| KDE Plasma (KWin) | Yes | Yes |
| Hyprland | No | Yes |
| Sway | No | Yes |
| Wayfire | No | Yes |
| river | No | Yes |
| niri | No | Yes |
| COSMIC | No | Yes |
| labwc | No | Yes |
| Miracle-WM | No | Yes |

All major Wayland compositors implement `zwlr_layer_shell_v1`. LayerShellQt is
just one client-side library that speaks this protocol — it can be replaced with
a direct implementation that has zero KDE dependencies.

### Additional benefits

- Smaller build dependency tree (drop KDE Frameworks requirement for daemon)
- Easier packaging for non-KDE distributions
- Opens the door to Hyprland/Sway/Wayfire compositor bridges
- No version coupling to KDE Plasma release cycle

## Current LayerShellQt Usage

### API Surface

PlasmaZones uses exactly **8 LayerShellQt APIs**:

| API | Protocol equivalent (`zwlr_layer_shell_v1`) | Usage count |
|---|---|---|
| `Window::get(window)` | Get/create `zwlr_layer_surface_v1` from `wl_surface` | ~12 |
| `setLayer()` | `zwlr_layer_surface_v1_set_layer` | ~12 |
| `setAnchors()` | `zwlr_layer_surface_v1_set_anchor` | ~12 |
| `setExclusiveZone()` | `zwlr_layer_surface_v1_set_exclusive_zone` | ~12 |
| `setKeyboardInteractivity()` | `zwlr_layer_surface_v1_set_keyboard_interactivity` | ~12 |
| `setScope()` | namespace argument in `get_layer_surface` | ~10 |
| `setScreen()` | `wl_output` argument in `get_layer_surface` | ~10 |
| `setMargins()` | `zwlr_layer_surface_v1_set_margin` | ~3 |

### Files affected (11 total)

**Core (1 file):**
- `src/core/screenmanager.cpp` — geometry sensor windows (LayerBackground,
  invisible, detect available screen area accounting for panels)

**Overlay service (7 files):**
- `src/daemon/overlayservice/internal.h` — `configureLayerShell()` helper,
  `getAnchorsForPosition()`, `centerLayerWindowOnScreen()`
- `src/daemon/overlayservice/overlay.cpp` — zone overlay windows (LayerOverlay,
  full-screen, transparent, no keyboard)
- `src/daemon/overlayservice/osd.cpp` — layout/navigation OSD (LayerTop,
  centered via margins, on-demand keyboard)
- `src/daemon/overlayservice/selector.cpp` — zone selector popup (LayerTop,
  edge-anchored, no keyboard)
- `src/daemon/overlayservice/selector_update.cpp` — selector position updates
- `src/daemon/overlayservice/snapassist.cpp` — snap assist + layout picker
  (LayerTop, full-screen, exclusive keyboard)
- `src/daemon/overlayservice/shader.cpp` — shader preview overlay

**Tests (1 file):**
- `tests/unit/ui/test_overlay_helpers.cpp`

**Build system (2 files):**
- `CMakeLists.txt` — `find_package(LayerShellQt 6.6 REQUIRED)`
- `src/CMakeLists.txt` — `LayerShellQt::Interface` linked to 3 targets
  (plasmazones_core PUBLIC, plasmazonesd PRIVATE, plasmazones-editor PRIVATE)

### Existing abstraction

There is **no abstraction layer**. LayerShellQt is used directly. However, the
`configureLayerShell()` helper in `internal.h` centralizes the most common
7-call pattern into a single function — this is where the new abstraction
boundary would go.

### Window types and their layer-shell configurations

| Window | QML source | Layer | Keyboard | Anchors | ExclusiveZone | Scope |
|---|---|---|---|---|---|---|
| Zone overlay | `RenderNodeOverlay.qml` / `ZoneOverlay.qml` | Overlay | None | All 4 edges | -1 | `plasmazones-overlay-{screen}` |
| Zone selector | `ZoneSelectorWindow.qml` | Top | None | Position-dependent | -1 | via `configureLayerSurface` |
| Layout OSD | `LayoutOsd.qml` | Overlay | None | All 4 + margins | -1 | via `configureLayerSurface` |
| Navigation OSD | `NavigationOsd.qml` | Overlay | None | All 4 + margins | -1 | via `configureLayerSurface` |
| Snap assist | `SnapAssistOverlay.qml` | Top | Exclusive | All 4 edges | -1 | via direct call |
| Layout picker | `LayoutPickerOverlay.qml` | Top | Exclusive | All 4 edges | -1 | via direct call |
| Shader preview | `RenderNodeOverlay.qml` | Overlay | None | All 4 edges | -1 | via `configureLayerSurface` |
| Geometry sensor | (plain QWindow) | Background | None | All 4 edges | 0 | `plasmazones-sensor-{screen}` |

## Proposed Replacement

### New class: `PlasmaZones::LayerSurface`

A single C++ class (~300 lines) that implements the `zwlr_layer_shell_v1` client
protocol directly using `libwayland-client`. The API mirrors LayerShellQt for
minimal migration effort.

```
src/core/layersurface.h      — public interface (~80 lines)
src/core/layersurface.cpp    — Wayland protocol implementation (~250 lines)
```

### Public interface

```cpp
namespace PlasmaZones {

class LayerSurface : public QObject
{
    Q_OBJECT
public:
    enum Layer {
        LayerBackground = 0,
        LayerBottom      = 1,
        LayerTop         = 2,
        LayerOverlay     = 3
    };

    enum KeyboardInteractivity {
        KeyboardNone      = 0,
        KeyboardExclusive = 1,
        KeyboardOnDemand  = 2
    };

    enum Anchor {
        AnchorTop    = 1,
        AnchorBottom = 2,
        AnchorLeft   = 4,
        AnchorRight  = 8
    };
    Q_DECLARE_FLAGS(Anchors, Anchor)

    static LayerSurface* get(QWindow* window);

    void setLayer(Layer layer);
    void setAnchors(Anchors anchors);
    void setExclusiveZone(int zone);
    void setKeyboardInteractivity(KeyboardInteractivity mode);
    void setScope(const QString& scope);
    void setScreen(QScreen* screen);
    void setMargins(const QMargins& margins);

signals:
    void configured(uint32_t serial, uint32_t width, uint32_t height);
    void closed();

private:
    explicit LayerSurface(QWindow* window);

    QWindow* m_window = nullptr;
    struct wl_surface* m_surface = nullptr;
    struct zwlr_layer_surface_v1* m_layerSurface = nullptr;

    Layer m_layer = LayerTop;
    Anchors m_anchors;
    int m_exclusiveZone = -1;
    KeyboardInteractivity m_keyboard = KeyboardNone;
    QString m_scope;
    QMargins m_margins;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(LayerSurface::Anchors)

} // namespace PlasmaZones
```

### Implementation approach

#### Protocol binding

The `zwlr_layer_shell_v1` protocol XML is vendored into the source tree and
processed by `wayland-scanner` at build time to generate C client stubs:

```
src/core/protocols/wlr-layer-shell-unstable-v1.xml   (from wayland-protocols)
```

CMake generates `wlr-layer-shell-unstable-v1-client-protocol.h` and
`wlr-layer-shell-unstable-v1-protocol.c` via:

```cmake
ecm_add_wayland_client_protocol(LAYER_SHELL_SRCS
    PROTOCOL src/core/protocols/wlr-layer-shell-unstable-v1.xml
    BASENAME wlr-layer-shell)
```

Or without ECM (for full KDE independence):

```cmake
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

add_custom_command(
    OUTPUT wlr-layer-shell-unstable-v1-client-protocol.h
           wlr-layer-shell-unstable-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header
            ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
            wlr-layer-shell-unstable-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code
            ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
            wlr-layer-shell-unstable-v1-protocol.c
    DEPENDS protocols/wlr-layer-shell-unstable-v1.xml
)
```

#### Qt Wayland integration

The critical challenge is obtaining the `wl_surface` from a `QWindow` and
creating a layer surface on it **before** Qt creates an `xdg_toplevel`.

Three approaches, in order of preference:

**Option A: Custom QPA shell integration plugin (~500 lines)**

Write a Qt Wayland shell integration plugin that registers with the
`zwlr_layer_shell_v1` global. When a window is marked for layer-shell (via a
dynamic property or the `LayerSurface::get()` call), the plugin creates a
`zwlr_layer_surface_v1` instead of an `xdg_toplevel`.

This is exactly what LayerShellQt does internally. It is the cleanest approach
and ensures proper window lifecycle management.

```
src/core/qpa/layershellintegration.h
src/core/qpa/layershellintegration.cpp
src/core/qpa/layershellwindow.h
src/core/qpa/layershellwindow.cpp
```

Pros: Correct lifecycle, works with Qt's window management.
Cons: Depends on `QtWaylandClient` private headers (already a dependency via
`Qt6GuiPrivate`).

**Option B: Post-creation surface replacement (~200 lines)**

Let Qt create the window normally (as `xdg_toplevel`), then immediately destroy
the xdg role and create a `zwlr_layer_surface_v1` on the same `wl_surface`.

This is the approach used by `gtk4-layer-shell`. It is simpler but involves a
brief flash/reposition as the window role changes.

Pros: Simpler implementation, no QPA plugin needed.
Cons: Hacky, potential visual glitch on first show, depends on compositor
handling role changes gracefully.

**Option C: Use QtWaylandClient private API directly (~150 lines)**

Access Qt's Wayland internals to get the `wl_surface` and `wl_display`, then
create the layer surface directly. Mark the window with a property before
`show()` so the implementation knows to intercept.

```cpp
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>

// Get native Wayland objects from Qt
auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(
    window->handle());
wl_surface* surface = waylandWindow->wlSurface();
wl_display* display = waylandWindow->display()->wl_display();
```

Pros: Simplest code, minimal new files.
Cons: Tight coupling to Qt internals, may break across Qt minor versions.

**Recommendation:** Option A (QPA plugin) for long-term stability. Option C as a
faster prototype path since `Qt6GuiPrivate` is already a dependency.

## Migration Plan

See [multi-compositor-implementation-plan.md](multi-compositor-implementation-plan.md)
for the full step-by-step plan.

### Phase 1: Interface + QPA Plugin + Migration (5-7 days)

1. Create `src/core/layersurface.h` with the interface shown above
2. Implement QPA shell integration plugin (Option A) — creates
   `zwlr_layer_surface_v1` directly via Wayland protocol
3. Implement `LayerSurface` backed by the QPA plugin
4. Vendor `wlr-layer-shell-unstable-v1.xml`, add `wayland-scanner` to build
5. Migrate all 11 files from `LayerShellQt::Window` to `LayerSurface`
6. Remove `LayerShellQt` from CMake entirely
7. Verify on KDE Plasma

No intermediate shim, no dual-backend, no `USE_LAYERSHELLQT` flag.
One implementation, everywhere.

### Phase 2: Multi-Compositor Testing + CI/Packaging (4-6 days)

| Compositor | Test | Priority |
|---|---|---|
| KDE Plasma (KWin) | Full regression — must match current behavior | P0 |
| Hyprland | Zone overlay, OSD, snap assist, geometry sensors | P0 |
| Sway | Zone overlay, OSD, snap assist, geometry sensors | P0 |
| Wayfire | Zone overlay, OSD | P1 |
| niri | Zone overlay, OSD | P1 |
| river | Zone overlay | P2 |
| COSMIC | Zone overlay | P2 |
| labwc | Zone overlay | P2 |

Update CI and packaging to remove LayerShellQt dep.

## Build System Changes

### Before

```cmake
# CMakeLists.txt
find_package(LayerShellQt 6.6 REQUIRED)

# src/CMakeLists.txt
target_link_libraries(plasmazones_core PUBLIC LayerShellQt::Interface)
target_link_libraries(plasmazonesd PRIVATE LayerShellQt::Interface)
target_link_libraries(plasmazones-editor PRIVATE LayerShellQt::Interface)
```

### After

```cmake
# CMakeLists.txt — LayerShellQt is gone entirely
find_package(Wayland REQUIRED COMPONENTS Client)
find_package(Qt6 REQUIRED COMPONENTS WaylandClient)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# src/CMakeLists.txt
target_link_libraries(plasmazones_core
    PUBLIC Wayland::Client
    PRIVATE Qt6::WaylandClientPrivate
)
target_sources(plasmazones_core PRIVATE
    core/layersurface.cpp
    core/qpa/layershellintegration.cpp
    core/qpa/layershellwindow.cpp
    ${LAYER_SHELL_GENERATED}
)
```

### New dependencies

| Dependency | Already required? | Notes |
|---|---|---|
| `libwayland-client` | Yes (transitive via Qt6) | Already present on any Wayland system |
| `wayland-scanner` | Yes (build-time, via ECM) | Can also use standalone |
| `Qt6::GuiPrivate` | Yes (for QRhi shaders) | Already a dependency |
| `Qt6::WaylandClient` (private) | **New** (for QPA integration) | Same class of dep as Qt6GuiPrivate |

## Remaining KDE Dependencies After This Change

Removing LayerShellQt does **not** make PlasmaZones fully KDE-independent. These
KDE dependencies remain:

| Dependency | Used by | Can be made optional? |
|---|---|---|
| `KF6::GlobalAccel` | Keyboard shortcuts (daemon) | Yes — fall back to compositor-native keybinds |
| `KF6::KCMUtils` | Settings KCM module | Yes — KCM is already a separate target |
| `ECM` (Extra CMake Modules) | Build system macros | Yes — replace with plain CMake |
| `KF6::I18n` | Translations (if used) | Yes — use Qt's built-in i18n |

A follow-up effort could make these optional behind build flags, enabling a
fully KDE-free build of the daemon:

```cmake
option(WITH_KDE "Build with KDE Framework integration" ON)

if(WITH_KDE)
    find_package(KF6GlobalAccel REQUIRED)
    find_package(KF6KCMUtils REQUIRED)
    # ...
endif()
```

## Compositor Bridge Strategy

With direct layer-shell support, the overlay system works everywhere. The
remaining compositor-specific integration is the **KWin effect plugin** which
handles window drag tracking, keyboard shortcuts, and window geometry
application. To support other compositors, additional bridges are needed:

```
PlasmaZones Daemon
├── Overlay system (layer-shell — works everywhere)
├── D-Bus API (works everywhere)
├── Unix socket IPC (new — for non-D-Bus compositors)
│
├── KWin bridge (existing KWin C++ effect plugin)
├── Hyprland bridge (new — IPC via Unix sockets)
├── Sway bridge (new — i3-compatible IPC)
└── Wayfire bridge (new — plugin or IPC)
```

This document covers the overlay/layer-shell layer only. Compositor bridges for
window management are a separate effort tracked independently.

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Qt private API breaks across versions | Medium | Medium | Pin to Qt 6.6+, test on 6.7/6.8; QPA plugin approach is more stable |
| Compositor-specific layer-shell quirks | Low | Low | Test matrix above; protocol is well-standardized |
| Performance regression vs LayerShellQt | Very Low | Low | Direct protocol calls are actually faster (no KDE middleware) |
| Geometry sensor behavior differs | Low | High | Sensor is critical for panel detection; extensive multi-compositor testing needed |
| KDE users hit regressions | Low | High | QPA plugin speaks the same protocol as LayerShellQt; extensive testing on KDE Plasma before merge |

## Effort Estimate

| Phase | Work | Time |
|---|---|---|
| Phase 1: Abstraction layer | Interface + LayerShellQt backend + migration | 2-3 days |
| Phase 2: Direct Wayland backend | Protocol impl + QPA integration | 3-5 days |
| Phase 3: Multi-compositor testing | Test on KWin, Hyprland, Sway, Wayfire, niri | 3-5 days |
| Phase 4: Build system + packaging | CMake options, CI, package scripts | 1-2 days |
| **Total** | | **~2-3 weeks** |

## References

- [zwlr-layer-shell-v1 protocol spec](https://wayland.app/protocols/wlr-layer-shell-unstable-v1)
- [LayerShellQt source](https://invent.kde.org/plasma/layer-shell-qt)
- [gtk4-layer-shell](https://github.com/wmww/gtk4-layer-shell) — similar approach for GTK
- [wlr-protocols](https://gitlab.freedesktop.org/wlroots/wlr-protocols) — protocol XML source
