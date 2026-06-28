// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phosphor Compositor Core — Full Lifecycle Implementation Plan

## Context

PlasmaZones needs a standalone Wayland compositor (`phosphor-compositor-core`) to gain full control over window decorations, tabbing, shading, effects, and plugin extensibility — capabilities that the current KWin effect plugin path cannot deliver due to KWin's API constraints.

The existing phosphor-* library stack (50+ libraries, ~100k+ LOC) provides the entire policy/shell layer: placement engines, zone management, snapping, animation, scripting, rules, shortcuts, config, IPC, and shell UI. All of this connects via `ICompositorBridge` — a 23-method interface that any compositor implements to plug in.

The KWin effect plugin remains a separate target. Both share the daemon via D-Bus.

**Rendering approach:** Qt RHI with Vulkan as the primary backend, leveraging the existing `phosphor-rendering` (ShaderNodeRhi) infrastructure.

**No external compositor frameworks (no wlroots, smithay, or aquamarine).** Direct use of C libraries: libwayland-server, libdrm, libgbm, libinput, libxkbcommon, libseat, libudev, libxcb, libpipewire.

---

## Directory Structure

```
libs/phosphor-compositor-core/
  src/backend/       — DRM/KMS, session, headless backends
  src/input/         — libinput, seat, focus management
  src/protocols/     — Server-side Wayland protocol implementations
  src/scene/         — Scene graph, damage tracking
  src/render/        — Qt RHI compositor output pipeline
  src/xwayland/      — XWayland bridge
  src/decorations/   — Custom SSD, tabbing, shading
  src/effects/       — Blur, shadow, rounded corners, dim
  src/plugin/        — Plugin host (tiered API)
  include/PhosphorCompositorCore/  — Public headers

src/compositor/    — The compositor binary (main(), wiring)
```

---

## Phase 0: Build Skeleton + Headless Backend

**Delivers:** CMake project, headless backend (virtual output, no DRM), libwayland-server event loop accepting client connections, minimal ICompositorBridge stub, the compositor binary.

**Depends on:** Nothing

**Technical approach:**
- Event loop: `wl_event_loop` as primary; bridged into QCoreApplication via `QSocketNotifier` on `wl_event_loop_get_fd()` (needed for D-Bus and Qt RHI)
- `IBackend` abstract interface with `HeadlessBackend` and later `DrmBackend` implementations
- Advertises `wl_compositor` and `wl_output` globals (output is virtual)

**Integrates:** None yet (pure bootstrap)

**Verify:** `wayland-info` connects and enumerates the virtual output

---

## Phase 1: Core Wayland Protocols

**Delivers:** Server-side `wl_compositor`, `wl_subcompositor`, `wl_shm`, `wl_output`, `xdg_wm_base`/`xdg_surface`/`xdg_toplevel`/`xdg_popup`. Scene graph tracking committed surface state. Double-buffered commit semantics.

**Depends on:** Phase 0

**Technical approach:**
- Scene graph node types: `SceneOutput`, `SceneSurface`, `SceneSubsurface`, `SceneLayer`, `SceneDecoration`
- Damage tracking via `pixman_region32_t` per-output union
- Frame callbacks dispatched on vsync (60Hz timer until Phase 2)
- Protocol code generated from wayland-protocols upstream via `wayland-scanner`

**Integrates:** `phosphor-wayland` protocol XMLs as reference only (server marshalling generated fresh)

**Verify:** `foot` (terminal) launches, receives configure, renders (software blit until Phase 3)

---

## Phase 2: DRM/KMS Backend + Session

**Delivers:** Atomic modesetting (libdrm), GBM buffer allocation, page-flip scheduling, multi-output hotplug (libudev), VT switching (libseat).

**Depends on:** Phase 0 (backend interface), Phase 1 (wl_output for mode advertisement)

**Technical approach:**
- Atomic modesetting only (no legacy path — all modern HW supports it)
- `GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING` for primary plane buffers
- Page-flip fd integrated into wayland event loop via `wl_event_loop_add_fd`
- `DrmScreenProvider : PhosphorScreens::IScreenProvider` translates DRM connectors → PhysicalScreen objects

**Integrates:** `phosphor-screens` (IScreenProvider, PhysicalScreen, ScreenManager)

**Verify:** Compositor launches on TTY, outputs enumerated, VT switch works, monitor hotplug detected

---

## Phase 3: Qt RHI Rendering Pipeline

**Delivers:** Surface compositing via Qt RHI/Vulkan, SHM texture upload, linux-dmabuf zero-copy import, damage-based partial rendering, direct scanout detection.

**Depends on:** Phase 1 (scene graph), Phase 2 (DRM render targets)

**Technical approach:**
- `QRhi::create(QRhi::Vulkan)` pointing at DRM device's Vulkan physical device
- GBM buffers → dma-buf fd → `VK_EXT_external_memory_dma_buf` → `QRhiTexture::createFrom()` as render target
- Single render pass per output: clear → paint surfaces back-to-front from scene graph (each surface = textured quad)
- Direct scanout: fullscreen surface with compatible format → assign to DRM plane, skip render

**Integrates:** `phosphor-rendering` (ShaderNodeRhi) — same QRhi instance used later for effects; `phosphor-animation` (IMotionClock) — frame timing feeds animation clocks

**Verify:** `weston-simple-shm` renders on hardware, `mpv --vo=gpu` renders zero-copy, steady vsync-locked framerate

---

## Phase 4: Input Pipeline + Seat

**Delivers:** libinput → pointer/keyboard/touch/tablet. libxkbcommon keymap + repeat. wl_seat protocol. Focus management (click-to-focus default). Cursor rendering (hardware DRM plane, software fallback). Pointer constraints + relative pointer.

**Depends on:** Phase 1 (surfaces for focus), Phase 2 (libseat for device access), Phase 3 (cursor rendering)

**Technical approach:**
- libinput `open_restricted` via libseat's `libseat_open_device`
- Configurable focus model: click-to-focus (default), focus-follows-mouse, sloppy focus
- `CompositorShortcutBackend : PhosphorShortcuts::IBackend` intercepts configured sequences before they reach clients

**Integrates:** `phosphor-shortcuts` (Registry, IBackend) — direct compositor-level shortcut grabs replacing KGlobalAccel

**Verify:** Mouse moves, clicks raise windows, keyboard reaches clients, key repeat works, shortcuts fire

---

## Phase 5: ICompositorBridge + PolicyEngine + D-Bus Service

**Delivers:** Full 23-method ICompositorBridge implementation backed by real state. In-process PolicyEngine (zones, snapping, placement, rules, autotile, navigation). Compositor-owned D-Bus service for external tools. Window lifecycle. Stacking order, identity, decoration management.

**Depends on:** Phase 1 (xdg_toplevel), Phase 4 (focus for activation)

**Technical approach:**
- `PhosphorCompositorBridge` with `WindowHandle = XdgToplevel*`
- Window identity: `appId` from `set_app_id()` + monotonic instanceId
- PolicyEngine links phosphor-zones, phosphor-snap-engine, phosphor-placement, phosphor-rule, phosphor-config directly (no IPC for policy decisions)
- Drag resolution, navigation, autotile all in-process (<1μs per call)
- Compositor claims `org.plasmazones.Service` on session bus
- D-Bus interfaces: Settings, Layouts, Rules, Windows, Zones, Control
- No daemon required in standalone mode — compositor handles persistence directly
- Session restore: save window→zone on shutdown, restore on startup

**Integrates:** `phosphor-compositor` (ICompositorBridge, DecorationManager), `phosphor-zones` (LayoutRegistry, ZoneDetector), `phosphor-snap-engine` (SnapEngine), `phosphor-placement` (WindowTrackingService), `phosphor-rule` (RuleEvaluator), `phosphor-config` (Store), `phosphor-protocol` (wire types)

**Verify:** Zone snapping works in-process (zero D-Bus), `phosphorctl settings set snapping.threshold 20` takes immediate effect, session restore round-trips, settings app live-updates compositor state

---

## Phase 6: Layer Shell + Session Lock + Supporting Protocols

**Delivers:** wlr-layer-shell server, ext-session-lock server, wl_data_device (clipboard + DnD), xdg-decoration (SSD negotiation), fractional-scale, viewporter, presentation-time, tearing-control, idle-notify, idle-inhibit, foreign-toplevel. Shell UI launches as privileged internal client. Zone overlay/highlight rendering (replaces daemon's OverlayService).

**Depends on:** Phases 1–5

**Technical approach:**
- Layer-shell surfaces inserted at their requested layer in scene graph; exclusive zones reduce available area
- Shell process (phosphor-shell) connects via real Wayland connection using phosphor-wayland client bindings
- Clipboard: wl_data_device_manager v3 + zwp_primary_selection
- Zone overlay (snap-on-hover highlight, snap-assist grid) rendered by compositor effects or shell process — NOT by a daemon

**Integrates:** `phosphor-shell` (ShellEngine, PanelWindow), `phosphor-layer` (SurfaceFactory, ILayerShellTransport), `phosphor-surfaces` (SurfaceManager), `phosphor-wayland` (client bindings for shell), `phosphor-service-idle`, `phosphor-service-lock`

**Verify:** Shell panel appears with exclusive zone, clipboard works cross-client, session lock blanks outputs, DnD works, zone highlights appear during drag (same-frame latency)

---

## Phase 7: Custom SSD + Window Tabbing + Shading + Effects

**Delivers:** Compositor-drawn decorations (title bar, buttons, resize handles). Tab groups (multiple surfaces in one frame, tab bar, switch). Window shading (collapse to title bar). Effects: blur, shadows, rounded corners, dim inactive. Zone-aware decoration state (reads from PolicyEngine directly, not via D-Bus).

**Depends on:** Phase 3 (rendering), Phase 5 (PolicyEngine + window state), Phase 6 (layer-shell for overlays)

**Technical approach:**
- SSD: scene graph nodes above each toplevel. QPainter-rendered text for title, icon quads for buttons.
- Tab groups: `TabGroup` owns N toplevels; only active tab's surface visible. Tab bar in SSD area.
- Shading: configure toplevel to title-bar height; clip surface in scene graph; animate with phosphor-animation curves.
- Blur: dual-kawase multi-pass via ShaderNodeRhi (render scene excluding surface → downsample → blur → upsample → sample under surface).
- Shadows: blurred rect or nine-patch behind each window.
- Rounded corners: stencil/alpha clip at corners.
- Zone-aware state: decoration color/highlight driven by PolicyEngine::zoneAssignmentChanged signal (in-process)

**Integrates:** `phosphor-animation` (curves, spring), `phosphor-rendering` (ShaderNodeRhi for blur/effects), `phosphor-compositor` (DecorationManager, DecorationDefaults), `phosphor-rule` (decoration overrides via PolicyEngine), `phosphor-theme`

**Verify:** Title bars render with working buttons, tab groups form/switch, shading animates, blur visible behind transparent terminals, shadows present, rounded corners, zone highlight color matches active zone

---

## Phase 8: XWayland + PipeWire Screen Sharing

**Delivers:** XWayland subprocess management, X11 WM via libxcb, window mapping, EWMH, selection bridging (clipboard), DnD bridging. PipeWire DMA-BUF frame export for screen sharing.

**Depends on:** Phases 3–6

**Technical approach:**
- Spawn `Xwayland -rootless -wm <fd>` with socketpair
- Minimal reparenting WM: override-redirect as overlays, managed windows get toplevel semantics
- Selection bridging: X11 atoms ↔ MIME types, INCR for large transfers
- PipeWire: per-output (and per-window) DMA-BUF export streams

**Integrates:** `phosphor-service-pipewire` (connection lifecycle), `phosphor-compositor` (ICompositorBridge::windowInfo for WM_CLASS)

**Verify:** Firefox/Steam render, clipboard works between X11/Wayland apps, OBS captures via PipeWire

---

## Phase 9: Tiered Plugin System

**Delivers:** Three-tier plugins:
1. **Scripts (Luau):** Sandboxed, hot-reload, rules/placement/events
2. **QML Widgets:** Panel applets, overlay widgets via existing PluginLoader/Registry
3. **C++ Extensions:** Full-power native plugins with render hooks, protocol extensions, input filters

**Depends on:** Phases 5, 6, 7

**Technical approach:**
- Tier 1: `LuauEngine` with compositor-specific prelude (`pluau.compositor.*`) exposing window queries/mutations
- Tier 2: Existing `PluginLoader` + `Registry<IBarWidgetFactory>` pattern, widgets hosted by ShellEngine
- Tier 3: New `ICompositorPlugin` interface loaded via QPluginLoader. Hooks: render pass insertion, protocol global registration, input filters, event subscriptions.
- Security: Luau sandboxed (memory + CPU), QML in shell process (isolated), C++ trusted (same address space)

**Integrates:** `phosphor-scripting` (LuauEngine, LuauWatchdog), `phosphor-registry` (PluginLoader, Registry, Manifest), `phosphor-shell` (ShellEngine), `phosphor-ipc` (plugin IPC channel)

**Verify:** Luau script auto-centers new windows, QML clock applet loads in panel, C++ wobbly-windows effect renders, hot-reload works

---

## Dependency Graph

```
Phase 0 ──→ Phase 1 ──→ Phase 2 ──→ Phase 3
                │            │           │
                │            └──→ Phase 4 ←──┘
                │                    │
                └────→ Phase 5 ←─────┘
                            │
                            v
                       Phase 6
                            │
                   ┌────────┼────────┐
                   v        v        v
              Phase 7   Phase 8   Phase 9
```

Phases 7, 8, and 9 are largely independent once Phase 6 lands.

---

## Critical Integration Files (existing)

- `libs/phosphor-compositor/include/PhosphorCompositor/ICompositorBridge.h` — The 23-method interface to implement
- `kwin-effect/kwin_compositor_bridge.h` — Reference bridge implementation (KWin)
- `libs/phosphor-compositor/include/PhosphorCompositor/DecorationManager.h` — Decoration state (reuse directly)
- `libs/phosphor-rendering/include/PhosphorRendering/ShaderNodeRhi.h` — Qt RHI rendering (effects build on this)
- `libs/phosphor-screens/include/PhosphorScreens/IScreenProvider.h` — DRM backend implements this
- `libs/phosphor-shortcuts/include/PhosphorShortcuts/IBackend.h` — Compositor shortcut backend
- `libs/phosphor-scripting/include/PhosphorScripting/LuauEngine.h` — Plugin tier 1
- `libs/phosphor-registry/include/PhosphorRegistry/PluginLoader.h` — Plugin tier 2/3

---

## Verification Strategy

- **Unit tests (Qt Test):** Scene graph math, damage regions, protocol state machines, focus routing logic
- **Integration tests:** Headless backend + wayland-client test harness (connect, create surfaces, verify events)
- **Render tests:** Headless + CPU readback, pixel-compare against reference images
- **Manual testing:** DRM backend on real hardware, progressive feature enablement
- **Existing test infrastructure:** Docker build (from CLAUDE.md) extended with new dependencies
