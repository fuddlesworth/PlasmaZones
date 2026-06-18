// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 9: Tiered Plugin System

## Deliverables

- Three-tier plugin architecture:
  - **Tier 1 (Luau scripts):** Sandboxed, hot-reload, window events/queries/mutations
  - **Tier 2 (QML widgets):** Panel applets, overlay widgets via phosphor-registry
  - **Tier 3 (C++ extensions):** Full-power native plugins with render/input/protocol hooks
- Plugin discovery, loading, lifecycle management
- Security boundaries (sandboxing per tier)
- Hot-reload for Tier 1 (file watcher → reload script without restart)
- Plugin configuration integration (per-plugin settings in config)

## Class Hierarchy

```
PluginHost
├── owns PluginLoader (discovery + loading)
├── owns Tier1Host (Luau script engine integration)
├── owns Tier2Host (QML widget host via shell process)
├── owns Tier3Host (C++ plugin loader)
├── manages plugin lifecycle (load, enable, disable, unload)
└── exposes PluginAPI (bridge between plugins and compositor internals)

Tier1Host
├── owns LuauEngine (from phosphor-scripting)
├── owns LuauWatchdog (memory + CPU limits)
├── owns ScriptRegistry (loaded scripts + their state)
├── provides Compositor Prelude (pluau.compositor.* globals)
├── supports hot-reload (file change → unload old, load new)
└── event dispatch: compositor events → subscribed scripts

Tier2Host
├── communicates with phosphor-shell process
├── shell loads QML plugins via PluginLoader
├── widgets appear as layer-shell surfaces
├── IPC channel for widget ↔ compositor state queries
└── relies on existing phosphor-registry infrastructure

Tier3Host
├── QPluginLoader for each .so/.dylib
├── instantiates ICompositorPlugin per plugin
├── manages hook registration (render, input, protocol, events)
├── plugin isolation: same address space (trusted)
└── versioned ABI (reject incompatible plugins)

ICompositorPlugin (abstract — C++ plugins implement this)
├── metadata() — name, version, description, author
├── initialize(PluginAPI*) — called on load
├── shutdown() — called on unload
├── Optional hooks (override to enable):
│   ├── renderHook() — insert draw commands into render pass
│   ├── inputFilter() — intercept/modify/consume input events
│   ├── protocolExtension() — register custom Wayland globals
│   └── eventSubscriptions() — subscribe to compositor events
└── ABI version checked at load time

PluginAPI (exposed to all tiers, tier-appropriate subset)
├── Window queries (list, info, geometry, state)
├── Window mutations (move, resize, close, minimize, maximize)
├── Screen queries (outputs, geometry, scale)
├── Event subscription (windowAdded, windowRemoved, focusChanged, etc.)
├── Configuration access (per-plugin settings)
├── Shortcut registration (bind key sequences to plugin actions)
├── Timer creation (one-shot and repeating)
└── D-Bus access (limited, for IPC with other processes)
```

## File Map

```
libs/phosphor-compositor-core/src/plugin/
├── CMakeLists.txt
├── plugin_host.h
├── plugin_host.cpp
├── plugin_api.h                — PluginAPI interface (shared across tiers)
├── plugin_api.cpp
├── plugin_loader.h             — Discovery + loading logic
├── plugin_loader.cpp

libs/phosphor-compositor-core/src/plugin/tier1/
├── tier1_host.h
├── tier1_host.cpp
├── compositor_prelude.h        — Luau global bindings for compositor access
├── compositor_prelude.cpp
├── script_registry.h           — Tracks loaded scripts + subscriptions
├── script_registry.cpp
├── hot_reload.h                — QFileSystemWatcher integration
├── hot_reload.cpp

libs/phosphor-compositor-core/src/plugin/tier2/
├── tier2_host.h
├── tier2_host.cpp
├── widget_ipc.h                — IPC channel: compositor ↔ shell (QML widgets)
└── widget_ipc.cpp

libs/phosphor-compositor-core/src/plugin/tier3/
├── tier3_host.h
├── tier3_host.cpp
├── icompositor_plugin.h        — Abstract C++ plugin interface
├── plugin_hooks.h              — Hook registration infrastructure
├── plugin_hooks.cpp
├── render_hook.h               — Render pass injection API
├── render_hook.cpp
├── input_filter.h              — Input event interception API
├── input_filter.cpp
├── protocol_extension.h        — Custom Wayland global registration
└── protocol_extension.cpp

include/PhosphorCompositorCore/
├── ICompositorPlugin.h         — Public C++ plugin interface (installed header)
└── PluginAPI.h                 — Public plugin API (installed header)
```

## Tier 1: Luau Scripts

### Compositor Prelude API

```lua
-- Available to all Tier 1 scripts as global `compositor`

-- Window queries
compositor.windows()                -- returns array of WindowHandle
compositor.activeWindow()           -- returns WindowHandle or nil
compositor.windowInfo(handle)       -- returns {appId, title, geometry, maximized, ...}
compositor.windowAt(x, y)           -- returns WindowHandle at screen position

-- Window mutations
compositor.moveWindow(handle, x, y)
compositor.resizeWindow(handle, w, h)
compositor.closeWindow(handle)
compositor.activateWindow(handle)
compositor.minimizeWindow(handle)
compositor.maximizeWindow(handle)
compositor.setWindowOpacity(handle, opacity)  -- 0.0-1.0
compositor.setWindowKeepAbove(handle, above)

-- Screens
compositor.screens()                -- returns array of ScreenInfo
compositor.primaryScreen()          -- returns ScreenInfo

-- Events (subscribe with callbacks)
compositor.onWindowAdded(function(handle) ... end)
compositor.onWindowRemoved(function(handle) ... end)
compositor.onWindowFocusChanged(function(handle) ... end)
compositor.onWindowMoved(function(handle, x, y) ... end)
compositor.onWindowResized(function(handle, w, h) ... end)

-- Shortcuts
compositor.registerShortcut("my-action", "Meta+Shift+C", function()
    -- shortcut triggered
end)

-- Timers
compositor.setTimeout(500, function() ... end)    -- one-shot, ms
compositor.setInterval(1000, function() ... end)  -- repeating, ms

-- Config (per-script settings from user config)
compositor.config.get("myKey", defaultValue)
compositor.config.set("myKey", value)

-- Logging
compositor.log("message")
compositor.warn("warning")
compositor.error("error")
```

### Example Script: Auto-Center New Windows

```lua
-- scripts/auto-center.luau
-- Auto-centers new windows on their target screen

compositor.onWindowAdded(function(handle)
    local info = compositor.windowInfo(handle)
    if info.maximized or info.fullscreen then
        return  -- don't touch maximized/fullscreen
    end

    local screen = compositor.screenForWindow(handle)
    if not screen then return end

    local cx = screen.x + (screen.width - info.width) / 2
    local cy = screen.y + (screen.height - info.height) / 2
    compositor.moveWindow(handle, cx, cy)
end)
```

### Example Script: Tiled Focus Navigation

```lua
-- scripts/focus-nav.luau
-- Directional focus switching (like i3/sway)

local function findNearest(direction)
    local active = compositor.activeWindow()
    if not active then return nil end

    local info = compositor.windowInfo(active)
    local candidates = {}

    for _, w in ipairs(compositor.windows()) do
        if w ~= active then
            local winfo = compositor.windowInfo(w)
            if direction == "left" and winfo.x + winfo.width <= info.x then
                table.insert(candidates, {w, info.x - (winfo.x + winfo.width)})
            elseif direction == "right" and winfo.x >= info.x + info.width then
                table.insert(candidates, {w, winfo.x - (info.x + info.width)})
            elseif direction == "up" and winfo.y + winfo.height <= info.y then
                table.insert(candidates, {w, info.y - (winfo.y + winfo.height)})
            elseif direction == "down" and winfo.y >= info.y + info.height then
                table.insert(candidates, {w, winfo.y - (info.y + info.height)})
            end
        end
    end

    table.sort(candidates, function(a, b) return a[2] < b[2] end)
    return candidates[1] and candidates[1][1] or nil
end

compositor.registerShortcut("focus-left", "Meta+H", function()
    local target = findNearest("left")
    if target then compositor.activateWindow(target) end
end)

compositor.registerShortcut("focus-right", "Meta+L", function()
    local target = findNearest("right")
    if target then compositor.activateWindow(target) end
end)

compositor.registerShortcut("focus-up", "Meta+K", function()
    local target = findNearest("up")
    if target then compositor.activateWindow(target) end
end)

compositor.registerShortcut("focus-down", "Meta+J", function()
    local target = findNearest("down")
    if target then compositor.activateWindow(target) end
end)
```

### Sandboxing

```cpp
class Tier1Host {
    void loadScript(const QString& path) {
        auto* engine = m_luauEngine.get();

        // Sandbox configuration (via phosphor-scripting LuauWatchdog)
        LuauWatchdog::Config sandbox;
        sandbox.memoryLimitBytes = 64 * 1024 * 1024;  // 64MB max per script
        sandbox.instructionLimit = 1'000'000;          // per callback invocation
        sandbox.allowedLibraries = {"math", "string", "table", "bit32"};
        // NO: io, os, debug, coroutine (security)

        engine->loadScript(path, sandbox);
        engine->registerPrelude(m_compositorPrelude.get());
    }
};
```

### Hot-Reload

```cpp
class HotReload : public QObject {
    Q_OBJECT
public:
    void watchDirectory(const QString& scriptDir);

private Q_SLOTS:
    void onFileChanged(const QString& path) {
        if (!path.endsWith(QLatin1String(".luau"))) return;

        // Debounce: editors often trigger multiple change events per save
        // (write temp → rename → delete old). Coalesce within 100ms window.
        m_pendingReloads.insert(path);
        m_debounceTimer.start(100);  // singleShot 100ms
    }

    void onDebounceTimeout() {
        for (const auto& path : std::as_const(m_pendingReloads)) {
            if (!QFile::exists(path)) {
                m_scriptRegistry->unload(path);
                qInfo() << "Script removed:" << path;
                continue;
            }
            auto backup = m_scriptRegistry->snapshot(path);
            m_scriptRegistry->unload(path);
            if (!m_tier1Host->loadScript(path)) {
                m_scriptRegistry->restore(path, std::move(backup));
                qWarning() << "Hot-reload failed (parse error), rolled back:" << path;
                continue;
            }
            qInfo() << "Hot-reloaded:" << path;
        }
        m_pendingReloads.clear();
    }

private:
    QFileSystemWatcher m_watcher;
    QTimer m_debounceTimer;         // singleShot, 100ms
    QSet<QString> m_pendingReloads; // paths waiting for debounce
    ScriptRegistry* m_scriptRegistry;
    Tier1Host* m_tier1Host;
};
```

## Tier 2: QML Widgets

### Architecture

```
Compositor Process                        phosphor-shell Process
      │                                          │
      │ (communicates via Wayland protocol        │
      │  + custom IPC channel)                   │
      │                                          │
      │                                          │ PluginLoader discovers QML plugins
      │                                          │ Loads via phosphor-registry
      │                                          │ Creates QML component instances
      │                                          │ Renders as layer-shell surfaces
      │                                          │
      │ ←── layer-shell surface (widget UI) ─────│
      │                                          │
      │ ←── IPC: requestWindowList ──────────────│
      │ ──→ IPC: windowList response ────────────│
      │                                          │
```

### Widget Manifest (reuses existing phosphor-registry format)

```json
{
    "id": "org.plasmazones.clock-widget",
    "name": "Clock Widget",
    "version": "1.0.0",
    "type": "bar-widget",
    "entry": "ClockWidget.qml",
    "tier": 2,
    "permissions": ["time", "locale"],
    "config": {
        "format": {"type": "string", "default": "HH:mm"},
        "showSeconds": {"type": "bool", "default": false}
    }
}
```

### IPC Protocol (compositor ↔ shell)

```cpp
// Custom Wayland protocol extension (phosphor_plugin_bridge_v1)
// Allows shell's QML widgets to query compositor state

// Requests (shell → compositor):
//   get_windows() → array of WindowInfo
//   get_active_window() → WindowInfo
//   subscribe_events(mask) → start receiving events
//   execute_action(action, params) → trigger compositor action

// Events (compositor → shell):
//   window_added(info)
//   window_removed(id)
//   window_state_changed(id, state)
//   active_window_changed(id)
```

## Tier 3: C++ Extensions

### ICompositorPlugin Interface

```cpp
// include/PhosphorCompositorCore/ICompositorPlugin.h
#pragma once

#include <QString>
#include <QJsonObject>

namespace PhosphorCompositorCore {

class PluginAPI;

struct PluginMetadata {
    QString id;          // "org.example.wobbly-windows"
    QString name;        // "Wobbly Windows"
    QString version;     // "1.0.0"
    QString author;
    QString description;
    int abiVersion;      // must match COMPOSITOR_PLUGIN_ABI_VERSION
};

#define COMPOSITOR_PLUGIN_ABI_VERSION 1

class ICompositorPlugin {
public:
    virtual ~ICompositorPlugin() = default;

    virtual PluginMetadata metadata() const = 0;
    virtual bool initialize(PluginAPI* api) = 0;
    virtual void shutdown() = 0;

    // Optional hooks — override to enable
    virtual void renderHook(RenderContext* ctx) { Q_UNUSED(ctx); }
    virtual bool inputFilter(InputEvent* event) { Q_UNUSED(event); return false; }
    virtual void protocolExtension(wl_display* display) { Q_UNUSED(display); }
};

} // namespace PhosphorCompositorCore

// Plugin entry point macro.
// Tier3Host calls createPlugin() and takes ownership of the returned pointer via unique_ptr.
#define PHOSPHOR_COMPOSITOR_PLUGIN(ClassName) \
    extern "C" Q_DECL_EXPORT PhosphorCompositorCore::ICompositorPlugin* createPlugin() { \
        return new ClassName(); \
    } \
    extern "C" Q_DECL_EXPORT int pluginAbiVersion() { \
        return COMPOSITOR_PLUGIN_ABI_VERSION; \
    }
```

### Render Hook API

```cpp
struct RenderContext {
    QRhi* rhi;
    QRhiCommandBuffer* commandBuffer;
    QRhiRenderTarget* renderTarget;
    IOutput* output;

    // Insert draw commands into the render pass at specific points:
    enum Phase {
        BeforeWindows,    // after background, before any window
        AfterWindows,     // after all windows, before overlay
        PerWindow,        // called for each window (access to surface texture)
    };
    Phase phase;

    // For PerWindow phase:
    WindowHandle window;            // which window
    QRhiTexture* surfaceTexture;    // the window's rendered content
    QRect geometry;                  // window position in output space

    // Set true in renderHook() to skip default draw for this window
    bool consumed = false;
};
```

### Input Filter API

```cpp
struct InputEvent {
    enum Type { PointerMotion, PointerButton, PointerAxis, Key, Touch };
    Type type;

    // Common
    uint32_t time;

    // Pointer
    QPointF position;
    uint32_t button;
    bool pressed;
    double axisValue;

    // Keyboard
    uint32_t keycode;
    uint32_t modifiers;
    bool keyPressed;

    // Result
    bool consumed = false;  // set true to prevent delivery to clients
};
```

### Example C++ Plugin: Wobbly Windows

```cpp
class WobblyWindowsPlugin : public PhosphorCompositorCore::ICompositorPlugin {
public:
    PluginMetadata metadata() const override {
        return {
            .id = QStringLiteral("org.plasmazones.wobbly-windows"),
            .name = QStringLiteral("Wobbly Windows"),
            .version = QStringLiteral("1.0.0"),
            .author = QStringLiteral("PlasmaZones"),
            .description = QStringLiteral("Deforms window meshes during move/resize"),
            .abiVersion = COMPOSITOR_PLUGIN_ABI_VERSION
        };
    }

    bool initialize(PluginAPI* api) override {
        m_api = api;
        m_api->subscribeEvent(Event::WindowMoveStarted, this, &WobblyWindowsPlugin::onMoveStarted);
        m_api->subscribeEvent(Event::WindowMoveStep, this, &WobblyWindowsPlugin::onMoveStep);
        m_api->subscribeEvent(Event::WindowMoveFinished, this, &WobblyWindowsPlugin::onMoveFinished);
        return true;
    }

    void shutdown() override {
        m_api->unsubscribeAll(this);
    }

    void renderHook(RenderContext* ctx) override {
        if (ctx->phase != RenderContext::PerWindow) return;

        auto it = m_activeDeforms.find(ctx->window);
        if (it == m_activeDeforms.end()) return;

        // Replace standard quad draw with deformed mesh
        const auto& mesh = it->mesh;
        // Render surface texture onto deformed grid mesh
        drawDeformedMesh(ctx, mesh);
        ctx->consumed = true;  // skip default quad draw for this window
    }

private:
    void onMoveStarted(WindowHandle handle) {
        // Initialize spring-mass grid for this window
        auto info = m_api->windowInfo(handle);
        m_activeDeforms[handle] = createSpringMesh(info.geometry.size(), m_gridSize);
    }

    void onMoveStep(WindowHandle handle, QPoint delta) {
        // Apply force to top edge of mesh (where title bar attaches)
        auto& deform = m_activeDeforms[handle];
        deform.applyForce(delta, SpringMesh::TopEdge);
        deform.simulate(16.0);  // one physics step (16ms)
    }

    void onMoveFinished(WindowHandle handle) {
        // Let mesh settle (continue simulating until springs at rest)
        m_activeDeforms[handle].startSettling();
    }

    struct SpringMesh {
        enum Edge { TopEdge, BottomEdge, LeftEdge, RightEdge };

        QList<QPointF> vertices;  // grid vertices (deformed positions)
        QList<QPointF> velocities;
        QList<QPointF> restPositions;
        int gridW, gridH;
        double stiffness = 0.5;
        double damping = 0.9;

        void applyForce(QPoint delta, Edge edge);
        void simulate(double dt);
        bool isSettled() const;
    };

    PluginAPI* m_api;
    QHash<WindowHandle, SpringMesh> m_activeDeforms;
    int m_gridSize = 8;  // 8x8 grid per window
};

PHOSPHOR_COMPOSITOR_PLUGIN(WobblyWindowsPlugin)
```

## Plugin Discovery + Loading

### Discovery Paths

```
Plugin search paths (in order):
  1. ~/.local/share/plasmazones/plugins/       (user-installed)
  2. /usr/share/plasmazones/plugins/           (system-wide)
  3. <build-dir>/plugins/                      (development)

Each plugin is a directory containing:
  - manifest.json (metadata, tier, entry point)
  - Plugin files:
    - Tier 1: *.luau scripts
    - Tier 2: *.qml + supporting files
    - Tier 3: lib<name>.so (or .dylib)
```

### Loading Sequence

```
Compositor startup:
  1. PluginHost::initialize()
  2. Scan discovery paths for manifest.json files
  3. Filter by:
     - ABI compatibility (Tier 3)
     - User enable/disable preferences (config)
     - Dependency resolution (plugins can depend on other plugins)
  4. Sort by load order (dependencies first)
  5. Load each plugin:
     Tier 1: LuauEngine::loadScript() + register prelude
     Tier 2: Send manifest to shell process via IPC
     Tier 3: QPluginLoader::load() + createPlugin() + initialize()
  6. All plugins loaded → emit PluginHost::ready()

Runtime enable/disable:
  - User can enable/disable plugins without restart
  - Tier 1: unload → load (hot-reload)
  - Tier 2: send enable/disable to shell
  - Tier 3: shutdown() + QPluginLoader::unload() (if plugin supports it)
```

## Security Model

```
Tier 1 (Luau) — SANDBOXED:
  ✓ Memory limit (64MB per script)
  ✓ CPU limit (instruction count per callback)
  ✓ No filesystem access
  ✓ No network access
  ✓ No raw pointer access
  ✓ Only compositor prelude API available
  ✓ Crash isolation (script error doesn't crash compositor)
  Risk: Low — can't damage system, only affect window state

Tier 2 (QML) — PROCESS ISOLATED:
  ✓ Runs in separate process (phosphor-shell)
  ✓ Communicates only via Wayland protocol + IPC channel
  ✓ No direct compositor memory access
  ✓ Widget crash doesn't crash compositor (shell restarts)
  ✓ Limited API surface via IPC
  Risk: Low — process boundary enforces isolation

Tier 3 (C++) — TRUSTED:
  ✗ Same address space as compositor
  ✗ Can crash the compositor
  ✗ Full memory access
  ✗ No sandboxing possible
  ✓ ABI version check (reject incompatible)
  ✓ Explicit user opt-in required to install
  Risk: High — only load plugins from trusted sources
  Mitigation: signed plugin packages (optional future enhancement)
```

## Plugin Configuration

```json
// ~/.config/plasmazones/plugins.json
{
    "enabled": [
        "org.plasmazones.auto-center",
        "org.plasmazones.clock-widget",
        "org.plasmazones.wobbly-windows"
    ],
    "disabled": [
        "org.plasmazones.focus-nav"
    ],
    "settings": {
        "org.plasmazones.auto-center": {
            "excludeApps": ["steam", "lutris"]
        },
        "org.plasmazones.clock-widget": {
            "format": "HH:mm:ss",
            "showSeconds": true
        },
        "org.plasmazones.wobbly-windows": {
            "stiffness": 0.7,
            "gridSize": 10
        }
    }
}
```

## Data Flow: Plugin Event Dispatch

```
Compositor Event (e.g., window added)
    │
    ▼
PluginHost::dispatchEvent(WindowAdded, handle)
    │
    ├──→ Tier1Host: for each subscribed script:
    │       LuauEngine::call(callback, {handle})
    │       (sandboxed, instruction-counted)
    │
    ├──→ Tier2Host: if any widget subscribed:
    │       IPC::send(WindowAdded, info)
    │       (async, non-blocking)
    │
    └──→ Tier3Host: for each subscribed C++ plugin:
            plugin->onEvent(WindowAdded, handle)
            (direct call, same thread)
```

## Verification

1. Luau script auto-centers new windows (user opens terminal → centered)
2. Luau hot-reload: edit script file → behavior changes without restart
3. Luau sandbox: script exceeding memory limit is killed, compositor survives
4. QML clock widget appears in shell panel with correct time
5. QML widget crash → shell restarts, widget reappears
6. C++ wobbly-windows: move window → visible deformation
7. C++ render hook: custom shader applied per-window
8. C++ input filter: consume specific key combo before client
9. Plugin enable/disable from settings UI works
10. Plugin with invalid ABI version rejected at load time
11. Unit tests:
    - `test_plugin_discovery` — scan paths, find manifests
    - `test_luau_prelude` — compositor.windows() returns correct data
    - `test_luau_sandbox` — memory limit triggers, script killed
    - `test_hot_reload` — modify file, verify new behavior active
    - `test_tier3_abi_check` — wrong ABI version → load rejected
    - `test_render_hook_ordering` — BeforeWindows/PerWindow/AfterWindows sequencing
    - `test_input_filter_consumption` — filter returns consumed=true → event not delivered
    - `test_event_dispatch` — event reaches all subscribed tiers
