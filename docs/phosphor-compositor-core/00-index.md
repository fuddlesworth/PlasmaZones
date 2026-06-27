// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phosphor Compositor Core — Technical Design Document

## Overview

A standalone Wayland compositor built from first principles using C libraries (libwayland-server, libdrm, libgbm, libinput, libxkbcommon, libseat, libudev, libxcb, libpipewire) with Qt RHI/Vulkan rendering. Integrates into the existing phosphor-* ecosystem via `ICompositorBridge`.

## Document Index

| File | Contents |
|------|----------|
| [01-architecture.md](01-architecture.md) | Global architecture, threading model, memory management, event loop design |
| [02-phase0-skeleton.md](02-phase0-skeleton.md) | Build skeleton, headless backend, event loop integration |
| [03-phase1-protocols.md](03-phase1-protocols.md) | Core Wayland protocol implementations, scene graph, surface state machine |
| [04-phase2-drm.md](04-phase2-drm.md) | DRM/KMS backend, session management, output lifecycle |
| [05-phase3-rendering.md](05-phase3-rendering.md) | Qt RHI/Vulkan rendering pipeline, texture management, damage |
| [06-phase4-input.md](06-phase4-input.md) | Input pipeline, seat, focus management, cursor |
| [07-phase5-bridge.md](07-phase5-bridge.md) | ICompositorBridge, PolicyEngine, D-Bus service, session restore |
| [08-phase6-shell.md](08-phase6-shell.md) | Layer shell, session lock, clipboard, supporting protocols |
| [09-phase7-decorations.md](09-phase7-decorations.md) | Custom SSD, window tabbing, shading, effects pipeline |
| [10-phase8-xwayland.md](10-phase8-xwayland.md) | XWayland bridge, PipeWire screen sharing |
| [11-phase9-plugins.md](11-phase9-plugins.md) | Tiered plugin system (Luau/QML/C++) |
| [12-policy-integration.md](12-policy-integration.md) | Policy engine integration, daemon boundary, D-Bus service |

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

## Key Constraints

- C++20, `namespace PhosphorCompositorCore` (internal); implements `PhosphorCompositor::ICompositorBridge`
- Qt6 for rendering (Qt RHI) and QML shell, NOT for core event loop
- libwayland-server owns the main event loop; Qt bridged via QSocketNotifier
- No wlroots, smithay, or aquamarine — direct C library usage only
- Implements `ICompositorBridge` (23 methods) — consumed in-process by PolicyEngine
- **No daemon in standalone mode** — compositor owns policy, persistence, and D-Bus service
- PolicyEngine links phosphor-zones, phosphor-snap-engine, phosphor-placement, phosphor-rule directly (zero IPC for frame-critical work)
- Compositor claims `org.plasmazones.Service` D-Bus name for settings app / KCM / phosphorctl
- KWin effect plugin continues as a separate target (talks to daemon, which still runs in KWin mode)
- SPDX: `LGPL-2.1-or-later` (library), `GPL-3.0-or-later` (compositor binary)
