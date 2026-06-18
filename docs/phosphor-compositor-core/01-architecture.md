// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Global Architecture

## System Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Compositor Binary                             │
│  src/compositor/main.cpp                                            │
│  Instantiates Compositor, wires subsystems, runs event loop         │
└────────────────────────────────┬────────────────────────────────────┘
                                 │ owns
┌────────────────────────────────▼────────────────────────────────────┐
│                    Compositor (top-level object)                     │
│  Owns: Backend, Server, SceneGraph, Renderer, InputManager,         │
│        WindowManager, ProtocolManager, PluginHost, ShellLauncher    │
└─┬──────────┬──────────┬──────────┬──────────┬──────────┬───────────┘
  │          │          │          │          │          │
  ▼          ▼          ▼          ▼          ▼          ▼
Backend   Server    SceneGraph  Renderer  InputMgr   WindowMgr
(DRM/     (wayland  (surface    (Qt RHI   (libinput  (stacking,
 Headless) server)   tree)       /Vulkan)  /seat)     identity)
```

## Threading Model

```
┌─────────────────────────────────────────────────────────────────┐
│ MAIN THREAD (single-threaded compositor)                         │
│                                                                   │
│ Event sources (all fd-based, polled by wl_event_loop):           │
│   • wayland client connections (wl_display fd)                   │
│   • DRM page-flip events (DRM fd)                                │
│   • libinput events (libinput fd)                                │
│   • libseat session events (libseat fd)                          │
│   • udev hotplug events (udev monitor fd)                        │
│   • timer sources (frame scheduling, key repeat)                 │
│   • Qt event processing (QSocketNotifier bridging)               │
│                                                                   │
│ Frame cycle:                                                      │
│   1. Poll all fds (wl_event_loop_dispatch)                       │
│   2. Process wayland requests (surface commits, protocol ops)    │
│   3. Process input events (route to surfaces)                    │
│   4. Compute damage (scene graph dirty tracking)                 │
│   5. Render damaged outputs (Qt RHI command submission)          │
│   6. Submit to DRM (atomic commit / page flip)                   │
│   7. Dispatch frame callbacks to clients                         │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ RENDER THREAD (Qt RHI internal — NOT ours to manage)             │
│                                                                   │
│ Qt RHI may internally use a render thread for Vulkan command      │
│ buffer submission. We submit via QRhi on the main thread;         │
│ QRhi handles synchronization internally.                          │
│                                                                   │
│ We NEVER call QRhi from any thread other than main.               │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ SHELL PROCESS (separate process, communicates via Wayland)        │
│                                                                   │
│ phosphor-shell runs as a privileged Wayland client.               │
│ Has its own QGuiApplication + QML engine.                         │
│ Communicates with compositor via Wayland protocols only.          │
│ Dies and restarts independently of compositor.                    │
└─────────────────────────────────────────────────────────────────┘
```

**Rule: The compositor is SINGLE-THREADED.** All state mutation happens on the main thread. No mutexes, no atomics for shared compositor state. This matches wlroots, KWin, and every production Wayland compositor.

## Memory Management

| Object type | Ownership | Destruction |
|-------------|-----------|-------------|
| `wl_resource` | libwayland ref-counted | `wl_resource_destroy()` via client disconnect or explicit destroy request |
| Scene graph nodes | Parent-based tree (QObject-like) | Parent destruction cascades; explicit `removeFromScene()` for reparenting |
| GPU textures (`QRhiTexture`) | `std::unique_ptr` in `SurfaceTexture` | Released when surface is destroyed or buffer replaced |
| GBM buffer objects | `unique_ptr<gbm_bo>` in `DrmOutput` | Released on swap or output teardown |
| DRM resources | RAII wrappers (`DrmConnector`, `DrmCrtc`, `DrmPlane`) | Released when output disconnected or compositor exits |
| Backend objects | `std::unique_ptr` in `Compositor` | Compositor destructor |
| Protocol globals | `wl_global*` destroyed in `Server::~Server()` | Server teardown |
| Window objects (`XdgToplevel`) | Owned by `WindowManager` | Removed on unmap or client disconnect |

**No manual delete. No raw owning pointers.** Everything is either:
1. `std::unique_ptr` (sole ownership)
2. Parent-based tree (scene graph nodes)
3. libwayland resource lifecycle (ref-counted with destroy listeners)
4. `QPointer`-style weak refs for cross-references (e.g., focus target)

## Event Loop Integration

```cpp
// Pseudocode: main event loop
int main() {
    QCoreApplication app(argc, argv);  // For D-Bus, Qt RHI init

    Compositor compositor;
    compositor.initialize();

    // Bridge wayland event loop into Qt
    auto* eventLoop = compositor.server().eventLoop();
    int waylandFd = wl_event_loop_get_fd(eventLoop);
    QSocketNotifier notifier(waylandFd, QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, [&]() {
        wl_event_loop_dispatch(eventLoop, 0);
        wl_display_flush_clients(compositor.server().display());
    });

    return app.exec();
}
```

## Error Handling Strategy

| Error domain | Handling |
|-------------|----------|
| Wayland protocol errors | `wl_resource_post_error()` → kills offending client only |
| DRM commit failure | Log, skip frame, retry next vblank. If persistent (3+ frames), fall back to different mode/plane config |
| GPU texture import failure | Fall back to SHM path (CPU upload). Log warning. |
| libinput error | Log, continue. Individual device errors don't crash compositor. |
| XWayland crash | Log, retry up to 3 times with exponential backoff (1s, 2s, 4s). Disable XWayland if all retries exhausted. X11 windows lost on crash. |
| Shell process crash | Log, restart. Layer-shell surfaces recreated by shell on reconnect. |
| Out-of-memory | Qt's allocator throws; catch at frame boundary, skip effects, log. Never crash. |
| Config file corruption | Fall back to defaults. Log error. Never crash on bad config. |

## Namespace Structure

```
PhosphorCompositor::              — Top-level (ICompositorBridge lives here already)
PhosphorCompositorCore::          — The new library's internal namespace
PhosphorCompositorCore::Backend   — DRM, headless, session
PhosphorCompositorCore::Input     — libinput, seat, cursor
PhosphorCompositorCore::Protocol  — Server-side protocol implementations
PhosphorCompositorCore::Scene     — Scene graph, damage, nodes
PhosphorCompositorCore::Render    — Qt RHI output pipeline
PhosphorCompositorCore::XWayland  — XWayland bridge
PhosphorCompositorCore::Decor     — SSD, tabbing, shading
PhosphorCompositorCore::Effects   — Blur, shadow, corners
PhosphorCompositorCore::Plugin    — Plugin host, API
```

## External Dependency Map

| Library | Headers | Linkage | Used by |
|---------|---------|---------|---------|
| libwayland-server | `<wayland-server-core.h>` | `wayland-server` | Server, Protocol |
| libdrm | `<xf86drm.h>`, `<xf86drmMode.h>` | `libdrm` | Backend::Drm |
| libgbm | `<gbm.h>` | `gbm` | Backend::Drm, Render |
| libEGL | `<EGL/egl.h>` | via Qt RHI | Render (fallback path) |
| Vulkan | `<vulkan/vulkan.h>` | via Qt RHI | Render |
| libinput | `<libinput.h>` | `libinput` | Input |
| libxkbcommon | `<xkbcommon/xkbcommon.h>` | `xkbcommon` | Input::Keyboard |
| libseat | `<libseat.h>` | `libseat` | Backend::Session |
| libudev | `<libudev.h>` | `libudev` | Backend::Drm (hotplug) |
| libxcb | `<xcb/xcb.h>` | `xcb`, `xcb-composite`, `xcb-xfixes` | XWayland |
| libpipewire | `<pipewire/pipewire.h>` | `libpipewire-0.3` | Render::PipeWire |
| pixman | `<pixman.h>` | `pixman-1` | Scene (damage regions) |
| Qt6::Core | `<QObject>` etc. | Qt6::Core | Everything |
| Qt6::Gui | `<QRhi>` etc. | Qt6::Gui, Qt6::GuiPrivate | Render |
| Qt6::DBus | `<QDBusConnection>` | Qt6::DBus | Bridge (daemon comms) |

## Build System

```cmake
# libs/phosphor-compositor-core/CMakeLists.txt (top-level)
cmake_minimum_required(VERSION 3.22)
project(PhosphorCompositorCore VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui DBus)
find_package(PkgConfig REQUIRED)

pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(LIBSEAT REQUIRED IMPORTED_TARGET libseat)
pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)

# Optional (Phase 8)
pkg_check_modules(XCB IMPORTED_TARGET xcb xcb-composite xcb-xfixes)
pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)

# Wayland protocol code generation
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
# ... protocol generation macros ...

add_subdirectory(src/backend)
add_subdirectory(src/scene)
add_subdirectory(src/protocols)
add_subdirectory(src/render)
add_subdirectory(src/input)
# Phase 7+
add_subdirectory(src/decorations)
add_subdirectory(src/effects)
# Phase 8
add_subdirectory(src/xwayland)
# Phase 9
add_subdirectory(src/plugin)
```
