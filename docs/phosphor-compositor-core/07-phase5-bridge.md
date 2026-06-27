// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 5: ICompositorBridge + PolicyEngine + D-Bus Service

## Deliverables

- Full 23-method `ICompositorBridge` implementation (`PhosphorCompositorBridge`)
- Window lifecycle management (map → tracked → unmap → destroyed)
- Window identity resolution (appId, class, instanceId)
- Window stacking order (z-order) management with raise/lower
- Window geometry commands (move, resize, maximize, minimize, fullscreen)
- In-process PolicyEngine (zone detection, snapping, placement, rules, navigation, autotile)
- Compositor-owned D-Bus service (`org.plasmazones.Service`) for settings app, KCM, phosphorctl
- Session restore (persist window→zone assignments across compositor restarts)
- `WindowManager` orchestrating focus, stacking, geometry, and rules

## Class Hierarchy

```
PhosphorCompositorBridge : PhosphorCompositor::ICompositorBridge
├── maps WindowHandle (void*) ↔ XdgToplevel*
├── implements all 23 virtual methods
├── emits bridge signals → PolicyEngine + D-Bus subscribers
└── receives external commands (settings app/phosphorctl via D-Bus) → applies to XdgToplevel configure

WindowManager
├── owns WindowList (ordered by stacking z-order)
├── owns ActivationPolicy (which window is active)
├── owns PlacementEngine integration (initial position)
├── coordinates with Seat (keyboard focus = active window)
└── applies Rules on map

WindowEntry
├── XdgToplevel* toplevel
├── WindowInfo cached state (appId, title, geometry, states)
├── uint64_t instanceId (monotonic, unique)
├── QUuid zoneId (if snapped)
├── TabGroup* tabGroup (if tabbed, Phase 7)
└── DecorationState (SSD params)

PolicyEngine
├── owns LayoutRegistry, SnapEngine, WindowTrackingService
├── owns RuleEvaluator, ConfigStore
├── drag lifecycle (beginDrag / detectZone / endDrag / cancelDrag)
├── navigation (move / focus / swap / rotate / floatToggle)
├── autotile (retileZone / retileAll)
└── settings (applySetting with immediate persist)

CompositorDbusService
├── claims org.plasmazones.Service on session bus
├── exposes /Settings, /Layouts, /Windows, /Zones, /Control
└── routes external calls → PolicyEngine methods
```

## File Map

```
libs/phosphor-compositor-core/src/bridge/
├── CMakeLists.txt
├── phosphor_compositor_bridge.h
├── phosphor_compositor_bridge.cpp
├── window_manager.h
├── window_manager.cpp
├── window_entry.h
├── window_entry.cpp
├── window_list.h               — Ordered list with z-order operations
├── window_list.cpp
├── policy_engine.h             — In-process PolicyEngine (zones, snap, rules, nav)
├── policy_engine.cpp
├── compositor_dbus_service.h   — D-Bus service (org.plasmazones.Service)
├── compositor_dbus_service.cpp
├── session_restore.h           — Window→zone persistence across restarts
├── session_restore.cpp
├── placement_integration.h     — Bridge to phosphor-placement-engine
├── placement_integration.cpp
├── rule_integration.h          — Bridge to phosphor-rule
└── rule_integration.cpp
```

## ICompositorBridge Method Mapping

```cpp
class PHOSPHORCOMPOSITORCORE_EXPORT PhosphorCompositorBridge : public PhosphorCompositor::ICompositorBridge {
    Q_OBJECT
public:
    // === Window Lookup ===
    QList<WindowHandle> windows() const override;
    // → iterate m_windowList, return toplevel pointers as void*

    WindowHandle activeWindow() const override;
    // → m_windowManager->activeWindow()->toplevel cast to void*

    WindowHandle windowAt(QPoint pos) const override;
    // → scene graph hit-test at pos, find owning XdgToplevel

    // === Window Identity ===
    WindowInfo windowInfo(WindowHandle handle) const override;
    // → lookup WindowEntry by handle, return cached WindowInfo

    QString windowClass(WindowHandle handle) const override;
    // → XdgToplevel::appId()

    QString windowTitle(WindowHandle handle) const override;
    // → XdgToplevel::title()

    QUuid windowId(WindowHandle handle) const override;
    // → WindowEntry::id (stable QUuid per window)

    // === Window Properties ===
    QRect windowGeometry(WindowHandle handle) const override;
    // → XdgToplevel scene node globalBounds + decoration offsets

    bool isWindowMaximized(WindowHandle handle) const override;
    // → XdgToplevel::states contains XDG_TOPLEVEL_STATE_MAXIMIZED

    bool isWindowMinimized(WindowHandle handle) const override;
    // → WindowEntry::minimized (tracked internally — no xdg_toplevel state for this)

    bool isWindowFullscreen(WindowHandle handle) const override;
    // → XdgToplevel::states contains XDG_TOPLEVEL_STATE_FULLSCREEN

    // === Window Filtering ===
    bool shouldManageWindow(WindowHandle handle) const override;
    // → true for all XdgToplevels (compositor manages everything)
    // → false for layer-shell, popups, internal surfaces

    bool isWindowOnCurrentDesktop(WindowHandle handle) const override;
    // → check WindowEntry::desktop matches current virtual desktop

    bool isWindowOnScreen(WindowHandle handle, const PhosphorScreens::PhysicalScreen& screen) const override;
    // → check if window geometry intersects screen geometry

    // === Window Actions ===
    void moveWindow(WindowHandle handle, QPoint pos) override;
    // → update XdgToplevel position in scene graph (floating mode)
    // → or send configure with new geometry

    void resizeWindow(WindowHandle handle, QSize size) override;
    // → send xdg_toplevel configure(size, states)

    void closeWindow(WindowHandle handle) override;
    // → xdg_toplevel_send_close(resource)

    void activateWindow(WindowHandle handle) override;
    // → WindowManager::activate(entry) → keyboard focus + raise + state change

    void minimizeWindow(WindowHandle handle) override;
    // → hide in scene graph, mark minimized, remove keyboard focus

    void maximizeWindow(WindowHandle handle) override;
    // → compute maximized geometry from output area, send configure

    // === D-Bus Integration ===
    QString dbusService() const override;
    // → "org.plasmazones.Service"

    QString dbusObjectPath(WindowHandle handle) const override;
    // → "/org/plasmazones/compositor/window/<instanceId>"

    // === Screen ===
    PhosphorScreens::PhysicalScreen screenForWindow(WindowHandle handle) const override;
    // → find output overlapping window center point, return PhysicalScreen

Q_SIGNALS:
    void windowAdded(WindowHandle handle);
    void windowRemoved(WindowHandle handle);
    void windowGeometryChanged(WindowHandle handle, QRect geometry);
    void windowStateChanged(WindowHandle handle);
    void activeWindowChanged(WindowHandle handle);
    void stackingOrderChanged();
};
```

## Window Lifecycle State Machine

```
                    XdgToplevel created
                          │
                          ▼
                   ┌──────────────┐
                   │  PENDING     │  (no initial configure acked yet)
                   │              │  — WindowEntry created
                   │              │  — Rules evaluated (initial state/geometry)
                   │              │  — PlacementEngine computes initial position
                   │              │  — configure sent with computed geometry + states
                   └──────┬───────┘
                          │ client acks configure + commits buffer
                          │ (OR: client destroys toplevel / disconnects → skip to DESTROYED)
                          ▼
                   ┌──────────────┐
                   │  MAPPED      │  (visible, managed, in stacking order)
                   │              │  — inserted into scene graph
                   │              │  — windowAdded signal emitted
                   │              │  — may be auto-activated (if rules say so)
                   └──────┬───────┘
                          │
              ┌───────────┼───────────┐
              │           │           │
              ▼           ▼           ▼
        [MINIMIZED]  [MAXIMIZED]  [FULLSCREEN]
         (hidden)    (fill area)   (fill output)
              │           │           │
              └───────────┼───────────┘
                          │ client destroys toplevel / xdg_surface / client disconnects
                          ▼
                   ┌──────────────┐
                   │  DESTROYED   │  (cleanup)
                   │              │  — removed from scene graph
                   │              │  — removed from stacking order
                   │              │  — windowRemoved signal emitted
                   │              │  — WindowEntry deleted
                   └──────────────┘
```

## Stacking Order Management

```cpp
class WindowList {
public:
    /// All windows in stacking order (bottom → top)
    const QList<WindowEntry*>& stackingOrder() const;

    /// Raise to top of stacking order
    void raise(WindowEntry* entry);

    /// Lower to bottom
    void lower(WindowEntry* entry);

    /// Place just above a reference window
    void placeAbove(WindowEntry* entry, WindowEntry* reference);

    /// Place just below a reference window
    void placeBelow(WindowEntry* entry, WindowEntry* reference);

    /// Insert new window at appropriate position
    /// (above last active, or according to rules)
    void insert(WindowEntry* entry, InsertPosition pos = InsertPosition::AboveActive);

    /// Remove from stacking order
    void remove(WindowEntry* entry);

    /// Recompute scene graph z-indices from stacking order
    void syncToSceneGraph(SceneTree* windowLayer);

private:
    QList<WindowEntry*> m_order;  // index 0 = bottom, last = top
};
```

### Scene Graph Synchronization

```
WindowList stacking order:
  [0] terminal  (bottom)
  [1] firefox
  [2] vscode    (top/active)

→ SceneGraph windowLayer children:
  terminal.sceneNode  → z = 0
  firefox.sceneNode   → z = 1
  vscode.sceneNode    → z = 2
```

Every `raise()` / `lower()` / `insert()` / `remove()` call triggers `syncToSceneGraph()` which updates z-indices and marks affected outputs as damaged.

## PolicyEngine (In-Process)

The compositor links the phosphor policy libraries directly. No daemon participates in
frame-critical operations.

### Wiring

```cpp
class PolicyEngine : public QObject {
    Q_OBJECT
public:
    explicit PolicyEngine(Compositor* compositor, QObject* parent = nullptr);

    // Zone Management
    void loadLayouts();
    void reloadLayouts();
    PhosphorZones::LayoutRegistry& layoutRegistry();

    // Snap/Placement
    PhosphorSnapEngine::SnapEngine& snapEngine();
    PhosphorPlacement::WindowTrackingService& windowTracking();

    // Rules
    PhosphorRule::RuleEvaluator& ruleEvaluator();
    void reloadRules();

    // Config
    PhosphorConfig::Store& configStore();

    // Drag (replaces D-Bus WindowDrag protocol)
    void beginDrag(WindowEntry* entry);
    QUuid detectZoneDuringDrag(QPointF cursorPos, IOutput* output);
    struct DragResult { QRect geometry; QUuid zoneId; bool skipAnimation = false; };
    DragResult endDrag(WindowEntry* entry, QPointF cursorPos);
    void cancelDrag();

    // Navigation (replaces D-Bus-routed navigation.cpp in daemon)
    void handleMove(Direction dir);
    void handleFocus(Direction dir);
    void handleSwap(Direction dir);
    void handleRotate(RotateDirection dir);
    void handleFloatToggle(WindowEntry* entry);

    // Autotile
    void retileZone(const QUuid& zoneId, IOutput* output);
    void retileAll(IOutput* output);

    // Settings (immediate in-memory + persist)
    void applySetting(const QString& key, const QVariant& value);

Q_SIGNALS:
    void zoneAssignmentChanged(WindowEntry* entry, QUuid zoneId);
    void layoutChanged(IOutput* output);
    void settingChanged(const QString& key, const QVariant& value);

private:
    std::unique_ptr<PhosphorZones::LayoutRegistry> m_layoutRegistry;
    std::unique_ptr<PhosphorSnapEngine::SnapEngine> m_snapEngine;
    std::unique_ptr<PhosphorPlacement::WindowTrackingService> m_windowTracking;
    std::unique_ptr<PhosphorRule::RuleEvaluator> m_ruleEvaluator;
    std::unique_ptr<PhosphorConfig::Store> m_configStore;
    Compositor* m_compositor;
};
```

### Window Map → Policy Flow

```
Client                  Compositor                           PolicyEngine
  │                        │                                      │
  │ xdg_toplevel.commit()  │                                      │
  │ (first buffer) ──────→ │                                      │
  │                        │ 1. Create WindowEntry                │
  │                        │ 2. ruleEvaluator().evaluate(entry) ──→│ in-process
  │                        │ 3. snapEngine().computePlacement() ──→│ in-process
  │                        │ 4. Insert into WindowList            │
  │                        │ 5. Sync scene graph                  │
  │                        │ 6. Emit zoneAssignmentChanged ──────→│ (persist)
  │  ←── configure(geom)──│                                      │
```

## D-Bus Service (Compositor-Owned)

The compositor claims `org.plasmazones.Service` and exposes interfaces for external tools.
No daemon runs in standalone mode.

### Registration

```cpp
class CompositorDbusService : public QObject {
    Q_OBJECT
public:
    explicit CompositorDbusService(PolicyEngine* engine, QObject* parent = nullptr);

    bool registerOnBus() {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.registerService(QStringLiteral("org.plasmazones.Service"))) {
            qWarning("D-Bus service name already claimed");
            return false;
        }
        bus.registerObject(QStringLiteral("/org/plasmazones/Settings"), m_settings);
        bus.registerObject(QStringLiteral("/org/plasmazones/Layouts"), m_layouts);
        bus.registerObject(QStringLiteral("/org/plasmazones/Windows"), m_windows);
        bus.registerObject(QStringLiteral("/org/plasmazones/Zones"), m_zones);
        bus.registerObject(QStringLiteral("/org/plasmazones/Control"), m_control);
        return true;
    }

private:
    PolicyEngine* m_engine;
    SettingsAdaptor* m_settings;
    LayoutsAdaptor* m_layouts;
    WindowsAdaptor* m_windows;
    ZonesAdaptor* m_zones;
    ControlAdaptor* m_control;
};
```

### Live-Update Flow

```
Settings app                    Compositor D-Bus               PolicyEngine
     │                              │                              │
     │ SetSetting(key, value) ─────→│                              │
     │                              │ applySetting(key, value) ───→│
     │                              │                              │ update in-memory
     │                              │                              │ write to disk
     │  ←── SettingChanged signal ──│                              │
     │                              │                              │
     │ (next drag uses new value immediately)                      │
```

### Persistence

Compositor reads/writes directly (no intermediary):
```
~/.local/share/plasmazones/layouts/     — Zone layout JSON files
~/.local/share/plasmazones/session/     — Session restore data
~/.config/plasmazones/config.json       — Settings
~/.config/plasmazones/rules.json  — Rules
```

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

On shutdown: save `{appId, windowClass, zoneId, geometry}` per window.
On startup: when a window maps with matching identity, snap to saved zone.

## WindowInfo Caching

```cpp
struct WindowEntry {
    XdgToplevel* toplevel;  // non-owning; valid while mapped (entry erased on toplevel destroy signal)
    uint64_t instanceId;
    QUuid id;  // stable UUID per window

    // Cached WindowInfo fields (updated on toplevel signals)
    QString appId;
    QString title;
    QRect geometry;
    bool maximized = false;
    bool minimized = false;
    bool fullscreen = false;
    int desktop = 0;
    QUuid zoneId;

    PhosphorCompositor::WindowInfo toWindowInfo() const;
    void updateFromToplevel();  // sync cache from XdgToplevel state
};
```

## Placement Integration

```cpp
class PlacementIntegration {
public:
    /// Compute initial position for a newly mapped window.
    QPoint computeInitialPosition(WindowEntry* entry, IOutput* targetOutput);

private:
    // Uses phosphor-placement-engine:
    // - Cascade: offset from last placed window
    // - Smart: avoid overlap with existing windows
    // - Center: center on output
    // - Under cursor: place near pointer
    enum PlacementStrategy { Cascade, Smart, Center, UnderCursor };
    PlacementStrategy m_strategy = Smart;
};
```

## Data Flow: Window Map → Zone Assignment

```
Client                  Compositor                    PolicyEngine (in-process)
  │                        │                              │
  │ xdg_toplevel.commit()  │                              │
  │ (first buffer) ──────→ │                              │
  │                        │ 1. Create WindowEntry        │
  │                        │ 2. ruleEvaluator.evaluate() ─→│ (returns rule result)
  │                        │ 3. snapEngine.placement() ───→│ (returns zone + geometry)
  │                        │ 4. Insert into WindowList    │
  │                        │ 5. Sync scene graph          │
  │                        │ 6. Persist assignment ────────→│ (session restore file)
  │  ←── configure(geom)──│                              │
  │                        │                              │
```

No IPC. No daemon. Single-process function call chain.

## Verification

1. Open multiple windows — they appear in correct stacking order
2. Click to raise — window moves to top of stack
3. `phosphorctl windows list` — returns correct window list via D-Bus
4. Close window — removed from stacking order
5. Zone snapping: drag window → zone highlight appears same-frame → drop snaps
6. Navigation: Meta+H/J/K/L moves focus between zones instantly
7. Rules: specific app opens maximized (per rule, in-process evaluation)
8. Settings app changes layout → compositor applies immediately (live-update)
9. Kill compositor → restart → windows restore to saved zones
10. Unit tests:
    - `test_window_list` — insert, raise, lower, remove ordering
    - `test_compositor_bridge` — all 23 methods return correct state
    - `test_policy_engine_drag` — begin/detect/end returns correct zone
    - `test_policy_engine_navigation` — move/focus/swap compute correct targets
    - `test_window_lifecycle` — pending → mapped → destroyed sequence
    - `test_placement` — initial position avoids overlaps
    - `test_dbus_service` — SetSetting round-trips correctly
    - `test_session_restore` — save/load preserves zone assignments
