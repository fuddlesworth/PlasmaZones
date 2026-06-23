<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-compositor

> Compositor-plugin SDK for Phosphor. Provides the interface contract,
> D-Bus client, and shared utilities a compositor plugin needs to give its
> users zone-based window management.

## Responsibility

Phosphor splits into a **daemon** (owns placement logic, zones, layouts,
settings) and a **compositor plugin** (observes windows, applies geometry,
renders overlays). This library is the plugin side of that split.

A compositor plugin links `PhosphorCompositor`, implements
`ICompositorBridge` (23 methods mapping native window handles to the
daemon's vocabulary), wires handler interfaces for callbacks, and lets
`DaemonClient` manage all D-Bus communication. The plugin never touches
placement logic directly. The daemon decides *where*, and the plugin
applies *how*.

## Key types

| Type | Purpose |
|------|---------|
| `ICompositorBridge` | Abstract interface a plugin implements for window lookup, identity, properties, filtering, actions |
| `DaemonClient` | Typed D-Bus client handling registration, service watching, reconnection, method calls, signal dispatch |
| `IDragHandler` | Callback interface for drag start/move/end/policy-change |
| `IGeometryHandler` | Callback interface for geometry apply, batch operations, raise/activate |
| `ILifecycleHandler` | Callback interface for window open/close/activate/float-change |
| `BorderState` + `AutotileStateHelpers` | Per-screen border-state value type plus pure helper functions |
| `FloatingCache` | Compositor-side mirror of daemon float state |
| `SnapAssistFilter` | Snap-assist candidate building via bridge |
| `TriggerParser` | Modifier/button activation matching from config |
| `DebouncedScreenAction` | Debounce utility for screen-change coalescing |
| `GeometryHelpers` | Fractional-scaling-safe rounding |

## Typical use

```cpp
#include <PhosphorCompositor/DaemonClient.h>
#include <PhosphorCompositor/ICompositorBridge.h>
#include <PhosphorCompositor/IGeometryHandler.h>

class MyBridge : public PhosphorCompositor::ICompositorBridge { /* ... */ };

class MyPlugin : public PhosphorCompositor::IGeometryHandler,
                 public PhosphorCompositor::IDragHandler,
                 public PhosphorCompositor::ILifecycleHandler
{
    PhosphorCompositor::DaemonClient m_client;
    MyBridge m_bridge;

    void init() {
        m_client.setGeometryHandler(this);
        m_client.setDragHandler(this);
        m_client.setLifecycleHandler(this);

        connect(&m_client, &PhosphorCompositor::DaemonClient::daemonReady,
                this, [this]() {
            m_client.registerBridge("river", 3, {"borderless", "animation"});
        });
    }

    // IGeometryHandler
    void onApplyGeometry(const PhosphorCompositor::GeometryRequest& req) override {
        auto* window = m_bridge.findWindowById(req.windowId);
        if (window) m_bridge.moveResize(window, req.geometry);
    }
    // ... other handlers
};
```

## Existing adapters

| Compositor | Adapter | Location |
|------------|---------|----------|
| KWin 6 | `KWinCompositorBridge` | `kwin-effect/kwin_compositor_bridge.{h,cpp}` |
| river | planned | — |

## Architecture

```
Daemon (org.plasmazones D-Bus service)
    ↕ D-Bus IPC
DaemonClient (this library)
    ↓ dispatches to
IDragHandler / IGeometryHandler / ILifecycleHandler
    ↓ implemented by
Compositor Plugin
    ↓ calls
ICompositorBridge (plugin implements, wraps native window APIs)
```

The daemon always runs. The plugin is stateless with respect to placement.
It applies what the daemon tells it and reports window events back.

## Dependencies

- `Qt6::Core`, `Qt6::Gui`, `Qt6::DBus`
- [`phosphor-protocol`](../phosphor-protocol/README.md), [`phosphor-identity`](../phosphor-identity/README.md), [`phosphor-animation`](../phosphor-animation/README.md)
