# Multi-Compositor Support: Implementation Plan

Parent document: [multi-compositor-support.md](multi-compositor-support.md)

## Goal

Replace the `LayerShellQt` KDE dependency with a direct `zwlr_layer_shell_v1`
Wayland protocol implementation, enabling PlasmaZones overlays to work on any
Wayland compositor (Hyprland, Sway, Wayfire, niri, COSMIC, etc.).

## Prerequisites

- [ ] Confirm `wayland-scanner` is available on CI runners
- [ ] Confirm `Qt6::WaylandClient` private headers are available in dev packages
- [ ] Read LayerShellQt source at `invent.kde.org/plasma/layer-shell-qt` to
      understand QPA shell integration internals

## Phase 1: Abstraction Layer (no behavior change)

**Goal**: Introduce `PlasmaZones::LayerSurface` interface that wraps LayerShellQt
today and can swap backends later. No user-visible change.

### Step 1.1: Create the interface header

**File**: `src/core/layersurface.h`

Define `PlasmaZones::LayerSurface` with the same enums and methods as
`LayerShellQt::Window`, matching the exact values so casts aren't needed:

```
Layer:                  Background=0, Bottom=1, Top=2, Overlay=3
KeyboardInteractivity:  None=0, Exclusive=1, OnDemand=2
Anchor:                 Top=1, Bottom=2, Left=4, Right=8
```

Methods (mirrors LayerShellQt::Window 1:1):
- `static LayerSurface* get(QWindow* window)`
- `setLayer(Layer)`
- `setAnchors(Anchors)`
- `setExclusiveZone(int32_t)`
- `setKeyboardInteractivity(KeyboardInteractivity)`
- `setScope(const QString&)`
- `setScreen(QScreen*)`
- `setMargins(const QMargins&)`

This is a thin wrapper — no new logic.

### Step 1.2: Create the LayerShellQt backend

**File**: `src/core/layersurface.cpp`

Implement `LayerSurface` by delegating every call to `LayerShellQt::Window`:

```cpp
LayerSurface* LayerSurface::get(QWindow* window)
{
    // LayerShellQt::Window is created/cached per QWindow
    auto* lsqt = LayerShellQt::Window::get(window);
    if (!lsqt) return nullptr;

    // Create or retrieve our wrapper
    auto* ls = window->findChild<LayerSurface*>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!ls) {
        ls = new LayerSurface(window, lsqt);
    }
    return ls;
}

void LayerSurface::setLayer(Layer layer)
{
    m_lsqt->setLayer(static_cast<LayerShellQt::Window::Layer>(layer));
}
// ... same pattern for all methods
```

Guarded by `#ifdef PLASMAZONES_USE_LAYERSHELLQT` (always defined in this phase).

### Step 1.3: Migrate all call sites

Replace `LayerShellQt::Window` with `PlasmaZones::LayerSurface` in all 11 files.
This is a mechanical find-and-replace:

| File | Changes |
|---|---|
| `src/daemon/overlayservice/internal.h` | Replace include, update `configureLayerShell()` helper, update `getAnchorsForPosition()` return type, update `centerLayerWindowOnScreen()` |
| `src/daemon/overlayservice/overlay.cpp` | Replace include, replace `LayerShellQt::Window::get()` → `LayerSurface::get()`, replace enum references |
| `src/daemon/overlayservice/osd.cpp` | Replace include, replace `LayerShellQt::Window::get()` → `LayerSurface::get()`, replace enum references |
| `src/daemon/overlayservice/selector.cpp` | Replace include, replace direct calls |
| `src/daemon/overlayservice/selector_update.cpp` | No direct LayerShellQt usage (uses helpers from internal.h) — verify |
| `src/daemon/overlayservice/snapassist.cpp` | Replace include, replace 2 direct `LayerShellQt::Window::get()` blocks |
| `src/daemon/overlayservice/shader.cpp` | Replace include, uses `configureLayerShell()` helper — verify |
| `src/core/screenmanager.cpp` | Replace include (`LayerShellQt/Window` + `LayerShellQt/Shell`), replace `LayerShellQt::Window::get(sensor)` block |
| `tests/unit/ui/test_overlay_helpers.cpp` | Replace include, update test references |
| `CMakeLists.txt` | No change yet (still REQUIRED) |
| `src/CMakeLists.txt` | Add `layersurface.cpp` to `plasmazones_core` sources |

### Step 1.4: Update `configureLayerShell()` helper

The central helper in `internal.h` becomes the primary consumer of
`LayerSurface`. Rename to `configureLayerSurface()` for clarity:

```cpp
// BEFORE
inline void configureLayerShell(QQuickWindow* window, QScreen* screen, int layer,
    int keyboardInteractivity, const QString& scope,
    LayerShellQt::Window::Anchors anchors = LayerShellQt::Window::Anchors())

// AFTER
inline void configureLayerSurface(QQuickWindow* window, QScreen* screen,
    LayerSurface::Layer layer, LayerSurface::KeyboardInteractivity keyboard,
    const QString& scope,
    LayerSurface::Anchors anchors = LayerSurface::Anchors())
```

Callers that pass raw `int` for layer/keyboard get updated to use the enum
directly — better type safety.

### Step 1.5: Verify

- [ ] Build succeeds with no warnings
- [ ] All existing tests pass
- [ ] Manual test on KDE Plasma: zone overlay, OSD, snap assist, selector,
      geometry sensors all behave identically

### Deliverables

```
New files:
  src/core/layersurface.h           (~100 lines)
  src/core/layersurface.cpp         (~120 lines)

Modified files:
  src/daemon/overlayservice/internal.h
  src/daemon/overlayservice/overlay.cpp
  src/daemon/overlayservice/osd.cpp
  src/daemon/overlayservice/selector.cpp
  src/daemon/overlayservice/snapassist.cpp
  src/daemon/overlayservice/shader.cpp
  src/core/screenmanager.cpp
  src/CMakeLists.txt
  tests/unit/ui/test_overlay_helpers.cpp
```

---

## Phase 2: Direct Wayland Backend

**Goal**: Implement `zwlr_layer_shell_v1` protocol client directly, making
LayerShellQt optional.

### Step 2.1: Vendor the protocol XML

**File**: `src/core/protocols/wlr-layer-shell-unstable-v1.xml`

Copy from `wlr-protocols` repo. Also need the dependency protocol:

**File**: `src/core/protocols/xdg-shell.xml` (if not already available via
system wayland-protocols)

### Step 2.2: Add CMake protocol generation

**File**: `CMakeLists.txt` (top-level)

```cmake
option(USE_LAYERSHELLQT
    "Use KDE LayerShellQt for layer-shell (OFF = direct Wayland protocol)" ON)

if(USE_LAYERSHELLQT)
    find_package(LayerShellQt 6.6 REQUIRED)
    add_compile_definitions(PLASMAZONES_USE_LAYERSHELLQT)
else()
    find_package(Wayland REQUIRED COMPONENTS Client)
    find_package(Qt6 REQUIRED COMPONENTS WaylandClient)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
endif()
```

**File**: `src/CMakeLists.txt`

```cmake
if(NOT USE_LAYERSHELLQT)
    set(_proto_dir ${CMAKE_CURRENT_SOURCE_DIR}/core/protocols)
    set(_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${_gen_dir})

    add_custom_command(
        OUTPUT ${_gen_dir}/wlr-layer-shell-client-protocol.h
               ${_gen_dir}/wlr-layer-shell-protocol.c
        COMMAND ${WAYLAND_SCANNER} client-header
                ${_proto_dir}/wlr-layer-shell-unstable-v1.xml
                ${_gen_dir}/wlr-layer-shell-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} private-code
                ${_proto_dir}/wlr-layer-shell-unstable-v1.xml
                ${_gen_dir}/wlr-layer-shell-protocol.c
        DEPENDS ${_proto_dir}/wlr-layer-shell-unstable-v1.xml
        COMMENT "Generating wlr-layer-shell Wayland protocol code"
    )

    set(LAYER_SHELL_GENERATED
        ${_gen_dir}/wlr-layer-shell-client-protocol.h
        ${_gen_dir}/wlr-layer-shell-protocol.c
    )
endif()
```

Update link targets:

```cmake
if(USE_LAYERSHELLQT)
    target_link_libraries(plasmazones_core PUBLIC LayerShellQt::Interface)
else()
    target_link_libraries(plasmazones_core
        PUBLIC Wayland::Client
        PRIVATE Qt6::WaylandClientPrivate
    )
    target_sources(plasmazones_core PRIVATE
        core/layersurface_wayland.cpp
        core/qpa/layershellintegration.cpp
        core/qpa/layershellwindow.cpp
        ${LAYER_SHELL_GENERATED}
    )
    target_include_directories(plasmazones_core PRIVATE ${_gen_dir})
endif()
```

### Step 2.3: Implement QPA shell integration

This is the core technical challenge. We need Qt to create a
`zwlr_layer_surface_v1` instead of an `xdg_toplevel` for our overlay windows.

**Approach**: Custom `QWaylandShellIntegration` plugin (same approach as
LayerShellQt internals).

**New files**:

```
src/core/qpa/layershellintegration.h      (~60 lines)
src/core/qpa/layershellintegration.cpp    (~150 lines)
src/core/qpa/layershellwindow.h           (~80 lines)
src/core/qpa/layershellwindow.cpp         (~200 lines)
```

**`LayerShellIntegration`** — registers with the Wayland compositor:
- Binds `zwlr_layer_shell_v1` global during registry enumeration
- Creates `LayerShellWindow` instances when Qt creates a window that has been
  marked for layer-shell (via `QWindow::setProperty("_pz_layershell", true)`)

**`LayerShellWindow`** — wraps a single layer surface:
- Extends `QtWaylandClient::QWaylandShellSurface`
- Creates `zwlr_layer_surface_v1` on the window's `wl_surface`
- Handles `configure` events (compositor tells us the size)
- Handles `closed` events (compositor dismissed the surface)
- Translates `LayerSurface` API calls to protocol requests
- Commits `wl_surface` after property changes

**Registration**: The integration is loaded via `QT_WAYLAND_SHELL_INTEGRATION`
environment variable or by calling
`QWaylandClientExtension::registerShellIntegration()` at startup.

### Step 2.4: Implement direct Wayland backend for `LayerSurface`

**File**: `src/core/layersurface_wayland.cpp`

When `PLASMAZONES_USE_LAYERSHELLQT` is NOT defined, `LayerSurface::get()`
creates/retrieves a `LayerShellWindow` via the QPA integration:

```cpp
LayerSurface* LayerSurface::get(QWindow* window)
{
    // Mark window for layer-shell before it's shown
    window->setProperty("_pz_layershell", true);

    auto* ls = window->findChild<LayerSurface*>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!ls) {
        ls = new LayerSurface(window);
    }
    return ls;
}

void LayerSurface::setLayer(Layer layer)
{
    m_layer = layer;
    if (m_shellWindow) {
        m_shellWindow->setLayer(layer);
    }
}
```

The `m_shellWindow` pointer is resolved when the QPA creates the platform
window (after `show()` is called).

### Step 2.5: Initialize integration at daemon startup

**File**: `src/daemon/main.cpp` (modify)

Before `QGuiApplication` is created:

```cpp
#ifndef PLASMAZONES_USE_LAYERSHELLQT
    // Register our layer-shell QPA integration
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "pz-layer-shell");
    // OR register programmatically after QGuiApplication
    LayerShellIntegration::registerPlugin();
#endif
```

### Step 2.6: Handle `wl_output` ↔ `QScreen` mapping

The protocol requires a `wl_output*` when creating a layer surface on a
specific screen. Qt maintains this mapping internally:

```cpp
// Get wl_output from QScreen
auto* waylandScreen = dynamic_cast<QtWaylandClient::QWaylandScreen*>(
    screen->handle());
wl_output* output = waylandScreen->output();
```

This is needed in `setScreen()` to bind the layer surface to the correct
output. Without it, the compositor chooses the output (usually primary).

### Step 2.7: Verify

- [ ] Build succeeds with `-DUSE_LAYERSHELLQT=OFF`
- [ ] Build succeeds with `-DUSE_LAYERSHELLQT=ON` (regression check)
- [ ] Manual test on KDE Plasma with both backends
- [ ] Zone overlay appears on correct screen
- [ ] Geometry sensors detect panel areas correctly
- [ ] OSD centers properly via margins
- [ ] Snap assist receives keyboard input (exclusive mode)
- [ ] Zone selector positions at screen edges correctly

### Deliverables

```
New files:
  src/core/protocols/wlr-layer-shell-unstable-v1.xml
  src/core/layersurface_wayland.cpp         (~250 lines)
  src/core/qpa/layershellintegration.h      (~60 lines)
  src/core/qpa/layershellintegration.cpp    (~150 lines)
  src/core/qpa/layershellwindow.h           (~80 lines)
  src/core/qpa/layershellwindow.cpp         (~200 lines)

Modified files:
  CMakeLists.txt                            (option + conditional find_package)
  src/CMakeLists.txt                        (conditional sources + links)
  src/core/layersurface.cpp                 (#ifdef for backend selection)
  src/daemon/main.cpp                       (integration registration)
```

---

## Phase 3: Multi-Compositor Testing

**Goal**: Verify overlays work correctly on each target compositor.

### Test Matrix

Each compositor gets tested for these overlay behaviors:

| Test Case | What to verify |
|---|---|
| Zone overlay visible | Full-screen transparent overlay appears above all windows |
| Zone overlay on correct screen | Multi-monitor: overlay appears on intended screen only |
| Zone highlight | Hovering over zones highlights them |
| Shader effects | GLSL shader overlays render correctly |
| OSD centered | Layout OSD appears centered on screen |
| OSD auto-dismiss | OSD disappears after timeout |
| Zone selector position | Selector appears at configured screen edge/corner |
| Snap assist keyboard | Snap assist captures keyboard exclusively |
| Geometry sensor | Background sensor detects panel exclusion zones |
| Window stacking | Overlay is above app windows, below lock screen |

### Compositor Priority

| Priority | Compositor | Install method | Notes |
|---|---|---|---|
| **P0** | KDE Plasma (KWin 6.x) | System install | Must match current behavior exactly |
| **P0** | Hyprland | `pacman -S hyprland` | Largest non-KDE Wayland community |
| **P0** | Sway | `pacman -S sway` | Reference wlroots compositor |
| **P1** | Wayfire | `pacman -S wayfire` | Plugin-friendly, GLSL shader compat |
| **P1** | niri | `pacman -S niri` | Growing community, scrolling WM |
| **P2** | river | `pacman -S river` | Zig/wlroots, external layouts |
| **P2** | COSMIC | AUR `cosmic-session` | Smithay-based, System76 |
| **P2** | labwc | `pacman -S labwc` | Minimal, Raspberry Pi OS default |

### Known Quirks to Test For

| Quirk | Compositors | Detail |
|---|---|---|
| Layer-shell namespace handling | Sway | Sway may use namespace for stacking decisions |
| Keyboard grab behavior | Hyprland | Exclusive keyboard may interact with global shortcuts |
| Multi-output protocol | niri | Independent-output model may affect sensor behavior |
| Fractional scaling | KWin, Hyprland | Layer surface size vs buffer size with fractional scale |
| Panel detection | All | Geometry sensor must correctly detect exclusion zones from other layer surfaces (panels, bars) |

### Test Environment Setup

For each compositor, create a minimal config that includes:
- A status bar (waybar, or compositor-native) to test panel exclusion
- Multiple virtual outputs if possible (for multi-monitor testing)
- A few test windows to verify overlay stacking

---

## Phase 4: Build System + CI + Packaging

**Goal**: Clean up build options, update CI, update package specs.

### Step 4.1: CMake option default

Initially default to `ON` (LayerShellQt) for safety:

```cmake
option(USE_LAYERSHELLQT
    "Use KDE LayerShellQt for layer-shell (OFF = direct protocol)" ON)
```

After multi-compositor testing passes, flip default to `OFF`.

### Step 4.2: Auto-detect KDE environment

Add convenience logic to auto-select backend based on environment:

```cmake
# Auto-detect: if LayerShellQt is found, use it; otherwise fall back to direct
if(NOT DEFINED USE_LAYERSHELLQT)
    find_package(LayerShellQt 6.6 QUIET)
    if(LayerShellQt_FOUND)
        set(USE_LAYERSHELLQT ON)
    else()
        set(USE_LAYERSHELLQT OFF)
    endif()
endif()
```

### Step 4.3: Update CI matrix

Add compositor-specific CI jobs:

```yaml
# .github/workflows/build.yml
strategy:
  matrix:
    include:
      - name: "KDE (LayerShellQt)"
        cmake_flags: "-DUSE_LAYERSHELLQT=ON"
        deps: "layer-shell-qt-devel"
      - name: "Generic Wayland (direct)"
        cmake_flags: "-DUSE_LAYERSHELLQT=OFF"
        deps: "wayland-devel qt6-wayland-devel"
```

### Step 4.4: Update packaging

**Arch PKGBUILD**: Add `USE_LAYERSHELLQT` option, make `layer-shell-qt`
optional dependency instead of required.

**Debian control**: Split into `plasmazones` (direct) and
`plasmazones-kde` (with LayerShellQt) packages, or make it a build flag.

**RPM spec**: Add `%bcond_with layershellqt` conditional.

**Nix**: Add `useLayerShellQt` option to the derivation.

### Step 4.5: Update documentation

- Update README with multi-compositor support section
- Add build instructions for non-KDE systems
- Document `-DUSE_LAYERSHELLQT=OFF` flag

---

## Phase 5: Future — Compositor Bridges (out of scope, tracked separately)

With layer-shell working everywhere, the next effort is window management
bridges. Each compositor needs a way to:

1. Detect window drag start/end (currently via KWin effect)
2. Apply window geometry (move/resize to zones)
3. Track window lifecycle (open/close/focus)
4. Register keyboard shortcuts

This is separate from overlay support and significantly more work per
compositor. Rough sketch:

```
src/bridges/
  icompositorbride.h            — interface
  kwin/kwinbridge.cpp           — existing KWin effect (refactored)
  hyprland/hyprlandbridge.cpp   — Hyprland IPC client
  sway/swaybridge.cpp           — i3-compatible IPC client
  wlroots/foreigntoplevel.cpp   — wlr-foreign-toplevel-management
```

---

## File Inventory (all phases)

### New Files

| File | Phase | Lines (est.) | Purpose |
|---|---|---|---|
| `src/core/layersurface.h` | 1 | ~100 | Interface |
| `src/core/layersurface.cpp` | 1 | ~120 | LayerShellQt backend |
| `src/core/layersurface_wayland.cpp` | 2 | ~250 | Direct Wayland backend |
| `src/core/protocols/wlr-layer-shell-unstable-v1.xml` | 2 | ~300 | Vendored protocol spec |
| `src/core/qpa/layershellintegration.h` | 2 | ~60 | QPA plugin header |
| `src/core/qpa/layershellintegration.cpp` | 2 | ~150 | QPA plugin impl |
| `src/core/qpa/layershellwindow.h` | 2 | ~80 | Shell surface header |
| `src/core/qpa/layershellwindow.cpp` | 2 | ~200 | Shell surface impl |
| `tests/unit/core/test_layersurface.cpp` | 2 | ~150 | Unit tests |

### Modified Files

| File | Phase | Nature of change |
|---|---|---|
| `src/daemon/overlayservice/internal.h` | 1 | Replace LayerShellQt types with LayerSurface |
| `src/daemon/overlayservice/overlay.cpp` | 1 | Replace include + direct calls |
| `src/daemon/overlayservice/osd.cpp` | 1 | Replace include + direct calls |
| `src/daemon/overlayservice/selector.cpp` | 1 | Replace include + direct calls |
| `src/daemon/overlayservice/snapassist.cpp` | 1 | Replace include + direct calls |
| `src/daemon/overlayservice/shader.cpp` | 1 | Replace include (uses helper) |
| `src/core/screenmanager.cpp` | 1 | Replace include + sensor setup |
| `tests/unit/ui/test_overlay_helpers.cpp` | 1 | Replace includes + references |
| `src/CMakeLists.txt` | 1+2 | Add sources, conditional linking |
| `CMakeLists.txt` | 2 | Add option, conditional find_package |
| `src/daemon/main.cpp` | 2 | Register QPA integration |
| `.github/workflows/build.yml` | 4 | Add matrix entries |
| `packaging/arch/PKGBUILD` | 4 | Optional LayerShellQt dep |
| `packaging/debian/control` | 4 | Optional LayerShellQt dep |
| `packaging/rpm/plasmazones.spec` | 4 | Conditional build |

## Timeline

| Phase | Effort | Dependencies |
|---|---|---|
| Phase 1: Abstraction | 2-3 days | None |
| Phase 2: Wayland backend | 4-6 days | Phase 1 |
| Phase 3: Testing | 3-5 days | Phase 2 |
| Phase 4: CI/packaging | 1-2 days | Phase 3 |
| **Total** | **~2-3 weeks** | |

Phase 1 can be merged independently as it has zero risk (pure refactor,
LayerShellQt still used). Phases 2-4 land together or incrementally behind
the `USE_LAYERSHELLQT` build flag.
