// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Policy Engine Integration + Daemon Boundary

## Context

PlasmaZones started as a KWin plugin. All policy logic (zone detection, snap resolution,
autotile, navigation, window rules, shortcuts) lives in the daemon (`plasmazonesd`) because
the KWin effect API cannot host it in-process. The plugin streams state to the daemon over
D-Bus; the daemon computes; the daemon signals back geometry commands.

With a standalone compositor, this architecture inverts. The compositor links the policy
libraries directly — zero IPC for frame-critical work. The daemon either doesn't run at all
(standalone mode) or runs only for the KWin plugin path.

## Design Decisions

1. **The compositor owns all policy decisions in standalone mode.** Zone detection, snap
   resolution, autotile, navigation, window rules, drag handling — all in-process.

2. **No daemon runs alongside the standalone compositor.** The compositor handles persistence
   (layout/rule/config file I/O) and exposes D-Bus interfaces for external tools directly.

3. **The daemon exists solely for the KWin plugin path.** When KWin is the compositor, the
   daemon runs the same policy libraries in its own process and serves the KWin plugin.

4. **Settings app talks to whoever is running.** A single well-known D-Bus service name
   (`org.plasmazones.Service`) is claimed by the compositor in standalone mode or the daemon
   in KWin mode. The settings app doesn't need to know which is active.

5. **Live-update via D-Bus.** Settings changes (from settings app or KCM) arrive as D-Bus
   method calls on the active service. The compositor applies them immediately in-memory
   and persists to disk. No propagation delay.

## Two Deployment Modes

```
Standalone Compositor Mode                  KWin Plugin Mode
─────────────────────────────               ─────────────────────
                                            
phosphor-compositor process:                KWin process:
  ├── Wayland/DRM/Input                       └── plasmazones-effect (thin D-Bus client)
  ├── PolicyEngine (in-process)
  │   ├── phosphor-zones                    plasmazonesd process:
  │   ├── phosphor-snap-engine                ├── PolicyEngine (same libraries)
  │   ├── phosphor-placement                  ├── D-Bus API (org.plasmazones.Service)
  │   ├── phosphor-windowrule                 ├── Persistence (layouts, rules, config)
  │   └── phosphor-config                     └── KWin plugin protocol
  ├── Persistence (direct file I/O)             (CompositorBridge, WindowDrag,
  ├── D-Bus API (org.plasmazones.Service)        WindowTracking interfaces)
  └── Shell process (layer-shell client)
                                            Settings app: talks to daemon
Settings app: talks to compositor
phosphorctl: talks to compositor            phosphorctl: talks to daemon
```

## PolicyEngine (In-Process)

### Class Structure

```cpp
namespace PhosphorCompositorCore {

class PolicyEngine : public QObject {
    Q_OBJECT
public:
    explicit PolicyEngine(Compositor* compositor, QObject* parent = nullptr);

    // --- Zone Management ---
    void loadLayouts();
    void reloadLayouts();
    PhosphorZones::LayoutRegistry& layoutRegistry();

    // --- Snap/Placement ---
    PhosphorSnapEngine::SnapEngine& snapEngine();
    PhosphorPlacement::WindowTrackingService& windowTracking();

    // --- Window Rules ---
    PhosphorWindowRule::RuleEvaluator& ruleEvaluator();
    void reloadRules();

    // --- Drag Resolution (replaces daemon's WindowDragAdaptor) ---
    struct DragResult {
        QRect geometry;
        QUuid zoneId;
        bool skipAnimation = false;
    };
    void beginDrag(WindowEntry* entry);
    QUuid detectZoneDuringDrag(QPointF cursorPos, IOutput* output);
    DragResult endDrag(WindowEntry* entry, QPointF cursorPos);
    void cancelDrag();

    // --- Navigation (replaces daemon's navigation.cpp) ---
    void handleMove(Direction dir);
    void handleFocus(Direction dir);
    void handleSwap(Direction dir);
    void handleRotate(RotateDirection dir);
    void handleFloatToggle(WindowEntry* entry);

    // --- Autotile ---
    void retileZone(const QUuid& zoneId, IOutput* output);
    void retileAll(IOutput* output);

    // --- Settings ---
    PhosphorConfig::Store& configStore();
    void applySetting(const QString& key, const QVariant& value);

Q_SIGNALS:
    void zoneAssignmentChanged(WindowEntry* entry, QUuid zoneId);
    void layoutChanged(IOutput* output);
    void settingChanged(const QString& key, const QVariant& value);

private:
    std::unique_ptr<PhosphorZones::LayoutRegistry> m_layoutRegistry;
    std::unique_ptr<PhosphorSnapEngine::SnapEngine> m_snapEngine;
    std::unique_ptr<PhosphorPlacement::WindowTrackingService> m_windowTracking;
    std::unique_ptr<PhosphorWindowRule::RuleEvaluator> m_ruleEvaluator;
    std::unique_ptr<PhosphorConfig::Store> m_configStore;

    Compositor* m_compositor;
};

} // namespace PhosphorCompositorCore
```

### File Map

```
libs/phosphor-compositor-core/src/policy/
├── CMakeLists.txt
├── policy_engine.h
├── policy_engine.cpp
├── drag_handler.h              — Drag lifecycle (begin/move/end/cancel)
├── drag_handler.cpp
├── navigation_handler.h       — Move/focus/swap/rotate logic
├── navigation_handler.cpp
├── autotile_handler.h         — Retile on window add/remove/resize
├── autotile_handler.cpp
├── rule_handler.h             — Apply rules on window map
├── rule_handler.cpp
├── session_restore.h          — Load/save window→zone on startup/shutdown
└── session_restore.cpp
```

### Dependencies (CMake)

```cmake
target_link_libraries(phosphor-compositor-core-policy
    PUBLIC
        PhosphorZones
        PhosphorSnapEngine
        PhosphorPlacement
        PhosphorWindowRule
        PhosphorConfig
    PRIVATE
        PhosphorEngine
        PhosphorScreens
        PhosphorIdentity
)
```

All of these are LGPL pure-logic libraries with no D-Bus assumptions.

## D-Bus Service (Compositor-Owned)

### Service Name

```
org.plasmazones.Service
```

Claimed by the compositor in standalone mode, by the daemon in KWin mode.
The settings app, KCM, and phosphorctl connect to this name regardless of which
process owns it.

### Interface Map

```
/org/plasmazones/
├── Settings                    org.plasmazones.Settings
│   ├── GetSetting(key) → value
│   ├── SetSetting(key, value)
│   ├── GetAllSettings() → map
│   └── signal SettingChanged(key, value)
│
├── Layouts                     org.plasmazones.Layouts
│   ├── ListLayouts() → array
│   ├── GetLayout(id) → json
│   ├── SaveLayout(json) → id
│   ├── DeleteLayout(id)
│   ├── SetActiveLayout(outputName, layoutId)
│   └── signal LayoutChanged(outputName, layoutId)
│
├── WindowRules                 org.plasmazones.WindowRules
│   ├── GetAllRules() → array
│   ├── SaveRule(json)
│   ├── DeleteRule(id)
│   └── signal RulesChanged()
│
├── Windows                     org.plasmazones.Windows
│   ├── ListWindows() → array<WindowInfo>
│   ├── GetWindow(id) → WindowInfo
│   ├── MoveWindow(id, x, y)
│   ├── ActivateWindow(id)
│   ├── CloseWindow(id)
│   ├── SnapToZone(windowId, zoneId)
│   ├── FloatWindow(id)
│   └── signal WindowStateChanged(id, state)
│
├── Zones                       org.plasmazones.Zones
│   ├── GetActiveZones(outputName) → array<ZoneGeometry>
│   ├── GetWindowZone(windowId) → zoneId
│   └── signal ZoneAssignmentChanged(windowId, zoneId)
│
└── Control                     org.plasmazones.Control
    ├── ReloadConfig()
    ├── ReloadLayouts()
    ├── Quit()
    └── Version() → string
```

### Implementation

```cpp
class CompositorDbusService : public QObject {
    Q_OBJECT
public:
    explicit CompositorDbusService(PolicyEngine* engine, QObject* parent = nullptr);

    bool registerOnBus();

private:
    PolicyEngine* m_engine;

    // Adaptors (generated from XML interface files)
    SettingsAdaptor* m_settings;
    LayoutsAdaptor* m_layouts;
    WindowRulesAdaptor* m_rules;
    WindowsAdaptor* m_windows;
    ZonesAdaptor* m_zones;
    ControlAdaptor* m_control;
};
```

## Drag Flow (Standalone Compositor)

```
User grabs window title bar
    │
    ▼
InputManager::handlePointerButton(BTN_LEFT, pressed=true)
    │ (hit-test title bar → start interactive move)
    ▼
PolicyEngine::beginDrag(entry)
    │ store pre-drag geometry, enter drag state
    │
    ▼ [every pointer motion event]
PolicyEngine::detectZoneDuringDrag(cursorPos, output)
    │ calls ZoneDetector directly (in-process, <1μs)
    │ returns QUuid of zone under cursor (or null)
    │
    ▼
DecorationRenderer::drawZoneHighlight(zoneGeometry)
    │ (render highlight in scene graph, same frame)
    │
    ▼ [pointer button release]
PolicyEngine::endDrag(entry, cursorPos)
    │ SnapEngine::resolve() → target zone + geometry
    │ apply geometry to xdg_toplevel configure
    │ emit zoneAssignmentChanged (for session restore)
    │
    ▼
Done. Zero IPC. Total latency: <100μs per motion event.
```

Compare to today: cursor position → D-Bus → daemon → zone detection → D-Bus signal back
→ plugin draws highlight. Round-trip: 1-5ms per event.

## Navigation Flow (Standalone Compositor)

```
User presses Meta+H (focus left):
    │
    ▼
CompositorShortcutBackend::tryConsume(keycode, modifiers)
    │ matches registered shortcut
    ▼
PolicyEngine::handleFocus(Direction::Left)
    │ 1. Get active window's zone
    │ 2. Find adjacent zone in direction
    │ 3. Find window in that zone
    │ 4. WindowManager::activate(targetWindow)
    │
    ▼
Done. No IPC. No daemon. Single function call chain.
```

## Settings Live-Update Flow

```
User changes "snap threshold" in settings app:
    │
    ▼
Settings app calls D-Bus:
    org.plasmazones.Service /org/plasmazones/Settings
    SetSetting("snapping.threshold", 20)
    │
    ▼
CompositorDbusService::setSetting("snapping.threshold", 20)
    │ 1. m_engine->applySetting(key, value)  — immediate in-memory update
    │ 2. m_engine->configStore().save()      — persist to disk
    │ 3. emit SettingChanged(key, value)     — notify other listeners
    │
    ▼
SnapEngine already using new threshold on next drag. Instant.
```

## Persistence

### File Paths (same as today)

```
~/.local/share/plasmazones/layouts/     — Zone layout JSON files
~/.local/share/plasmazones/session/     — Session restore (window→zone mappings)
~/.config/plasmazones/config.json       — Settings
~/.config/plasmazones/windowrules.json  — Window rules
```

The compositor reads/writes these directly. No intermediary.

### Session Restore

```cpp
class SessionRestore {
public:
    explicit SessionRestore(const QString& savePath);

    void save(const QList<WindowEntry*>& windows);
    QHash<WindowIdentity, QUuid> load();

private:
    QString m_savePath;  // ~/.local/share/plasmazones/session/
};
```

## KWin Plugin Compatibility

### What doesn't change

The KWin plugin (`plasmazones-effect`) continues to talk to the daemon exactly as today.
The daemon continues to run the full PolicyEngine in-process (for the KWin path). The
D-Bus interfaces between plugin and daemon are unchanged:

- `org.plasmazones.CompositorBridge` — registration
- `org.plasmazones.WindowDrag` — begin/update/end
- `org.plasmazones.WindowTracking` — metadata push, geometry apply signals

### Service name arbitration

```cpp
// In compositor main():
if (!QDBusConnection::sessionBus().registerService(
        QStringLiteral("org.plasmazones.Service"))) {
    // Another instance (daemon) already has it. This is fine if we're
    // somehow in a mixed environment. Log a warning but continue —
    // the compositor still works, just without external D-Bus control.
    qWarning("org.plasmazones.Service already claimed — external tools "
             "will talk to the existing owner");
}
```

The daemon checks the same way. First process to claim wins. In normal operation:
- Standalone mode: compositor starts first, claims the name.
- KWin mode: daemon starts (autostart), claims the name. Compositor (KWin) doesn't try.

## Phase Integration

This design integrates with the existing phase plan as follows:

| Phase | Change |
|-------|--------|
| Phase 5 (Bridge) | `DaemonConnection` class becomes optional/removed. PolicyEngine replaces it. D-Bus service exposed by compositor instead. |
| Phase 6 (Shell) | Zone overlay/highlight rendering moves to shell process or compositor effects (no daemon overlay). |
| Phase 7 (Decorations) | Zone-aware decoration state read from PolicyEngine directly, not via D-Bus. |
| Phase 9 (Plugins) | Luau scripts access PolicyEngine via compositor prelude (zone queries, assignment mutations). |

## Verification

1. Drag window → zone highlight appears with <16ms latency (same-frame)
2. Drop in zone → window snaps immediately, no flicker
3. `phosphorctl settings set snapping.threshold 30` → immediate effect
4. Kill compositor → session restore file exists with correct data
5. Restart compositor → windows restore to saved zones
6. Settings app opens, changes layout → compositor reacts immediately
7. `phosphorctl windows list` → returns correct window state
8. Same settings app works against daemon in KWin mode (unchanged behavior)
9. Unit tests:
   - `test_policy_engine_drag` — begin/detect/end lifecycle, correct zone returned
   - `test_policy_engine_navigation` — move/focus/swap compute correct targets
   - `test_dbus_service` — SetSetting round-trips, SettingChanged emitted
   - `test_session_restore` — save/load cycle preserves zone assignments
   - `test_service_name_arbitration` — only one process claims the name
