# PlasmaZones Codebase Analysis for Autotiling Integration

**Analysis Date:** 2026-01-24
**Analyst:** Hive Mind Swarm - ANALYST Role
**Objective:** Identify architecture and integration points for autotiling functionality

---

## Executive Summary

PlasmaZones is a KDE/KWin window management tool that provides zone-based window snapping similar to Microsoft PowerToys FancyZones. The codebase follows a clean separation of concerns with a daemon process handling core logic and a KWin C++ effect plugin detecting window events. The architecture is well-suited for autotiling extension with clear integration points in the Layout, ZoneDetector, and WindowTracking components.

---

## Architecture Overview

### Component Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                         KWin Effect Plugin                          │
│  (kwin-effect/plasmazoneseffect.cpp)                               │
│  - Window drag detection via polling                                │
│  - Keyboard modifier tracking                                       │
│  - Navigation signals (moveWindowToZone, focusWindowInZone)        │
└─────────────────────┬───────────────────────────────────────────────┘
                      │ D-Bus IPC
                      ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         PlasmaZones Daemon                          │
│  (src/daemon/daemon.cpp)                                            │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐      │
│  │LayoutManager │  │ ZoneDetector │  │ WindowTrackingAdaptor│      │
│  │              │  │              │  │                      │      │
│  │ - Layouts    │  │ - Zone@point │  │ - Window assignments │      │
│  │ - Zones      │  │ - Adjacency  │  │ - Snap history       │      │
│  └──────┬───────┘  └──────────────┘  └──────────────────────┘      │
│         │                                                           │
│  ┌──────▼───────┐  ┌──────────────┐  ┌──────────────────────┐      │
│  │   Layout     │  │ScreenManager │  │   OverlayService     │      │
│  │              │  │              │  │                      │      │
│  │ - Zones[]    │  │ - Screens    │  │ - QML overlays       │      │
│  │ - Relative   │  │ - Panels     │  │ - Zone highlighting  │      │
│  │   geometry   │  │ - Available  │  │                      │      │
│  └──────────────┘  │   geometry   │  └──────────────────────┘      │
│                    └──────────────┘                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### Design Patterns

1. **Dependency Injection**: Components use `std::unique_ptr` ownership with interface pointers passed to dependent classes
2. **Interface Segregation**: `ISettings`, `ILayoutManager`, `IOverlayService` interfaces in `src/core/interfaces.h`
3. **Adaptor Pattern**: D-Bus adaptors wrap core classes for IPC exposure
4. **Observer Pattern**: Qt signals/slots for event propagation

---

## Key Classes and Responsibilities

### Core Classes (`src/core/`)

#### Zone (`zone.h`)
The fundamental unit representing a screen region where windows can be snapped.

**Key Properties:**
- `m_id` (QString) - Unique identifier
- `m_zoneNumber` (int) - Display number for keyboard shortcuts
- `m_relativeGeometry` (QRectF) - Normalized 0.0-1.0 coordinates
- `m_appearance` (ZoneAppearance) - Visual styling
- `m_highlighted` (bool) - Active state for overlay

**Key Methods:**
```cpp
QRectF relativeGeometry() const;
void setRelativeGeometry(const QRectF &geometry);
QRect absoluteGeometry(const QRect &screenGeometry) const;
```

**Autotiling Relevance:** Zones are the target regions. Autotiling would need to dynamically create/modify zones based on window count.

---

#### Layout (`layout.h`)
A collection of zones that defines a complete screen arrangement.

**Key Properties:**
- `m_zones` (QList<Zone*>) - Owned zone objects
- `m_zonePadding` (int) - Gap between zones
- `m_type` (QString) - Layout type identifier

**Key Methods:**
```cpp
void recalculateZoneGeometries(const QRect &screenGeometry);
Zone* addZone(const QRectF &relativeGeometry);
void removeZone(Zone *zone);
Zone* zoneAt(const QPointF &relativePoint) const;
```

**Autotiling Relevance:** This is a PRIMARY integration point. Autotiling would dynamically modify the zone list and call `recalculateZoneGeometries()` to update absolute positions.

---

#### LayoutManager (`layoutmanager.h`)
Manages multiple layouts and their assignments to screens/desktops/activities.

**Key Properties:**
- `m_layouts` (QHash<QString, Layout*>) - All loaded layouts
- `m_assignments` (QHash<AssignmentKey, QString>) - Screen/desktop/activity to layout mapping

**Key Methods:**
```cpp
Layout* layoutForScreen(QScreen *screen, int virtualDesktop = -1) const;
void setLayoutForScreen(const QString &layoutId, QScreen *screen, int desktop);
Layout* createLayout(const QString &name);
```

**Autotiling Relevance:** Would need to track which layouts are "autotiling" vs "static" layouts.

---

#### ZoneDetector (`zonedetector.h`)
Determines which zone contains a given point.

**Key Methods:**
```cpp
Zone* zoneAtPosition(const QPointF &cursorPos, Layout *layout, QScreen *screen) const;
QString findAdjacentZone(const QString &currentZoneId, const QString &direction, Layout *layout) const;
```

**Autotiling Relevance:** Zone detection logic can be reused. May need enhancement to detect "empty" zones for window placement.

---

#### ScreenManager (`screenmanager.h`)
Manages screen geometry with panel awareness.

**Key Methods:**
```cpp
QRect actualAvailableGeometry(QScreen *screen) const;
void updateAvailableGeometry(QScreen *screen);
```

**Autotiling Relevance:** Critical for calculating usable screen area excluding panels/docks.

---

### Daemon Classes (`src/daemon/`)

#### Daemon (`daemon.cpp`)
Central coordinator that initializes all services and connects signals.

**Initialization Pattern:**
```cpp
Daemon::Daemon(QObject *parent)
    : QObject(parent)
    , m_layoutManager(std::make_unique<LayoutManager>(nullptr))
    , m_zoneDetector(std::make_unique<ZoneDetector>(nullptr))
    , m_settings(std::make_unique<Settings>(nullptr))
    , m_overlayService(std::make_unique<OverlayService>(nullptr))
    , m_screenManager(std::make_unique<ScreenManager>(nullptr))
    , m_virtualDesktopManager(std::make_unique<VirtualDesktopManager>(...))
```

**Autotiling Relevance:** New autotiling service would be instantiated here and connected to window events.

---

#### WindowTrackingAdaptor (`windowtrackingadaptor.h`)
D-Bus adaptor exposing window-zone assignment tracking.

**Key D-Bus Methods:**
```cpp
void windowSnappedToZone(const QString &windowId, const QString &zoneId);
void windowRemovedFromZone(const QString &windowId);
QString getWindowZone(const QString &windowId) const;
QStringList getWindowsInZone(const QString &zoneId) const;
```

**Autotiling Relevance:** PRIMARY integration point. Autotiling needs to know when windows enter/leave zones to recalculate layouts.

---

#### OverlayService (`overlayservice.h`)
Manages QML overlay windows for zone visualization.

**Key Methods:**
```cpp
void show();
void hide();
void highlightZone(const QString &zoneId);
void updateLayout(Layout *layout);
```

**Autotiling Relevance:** Would need to show real-time previews of autotiled layouts during window placement.

---

### KWin Effect (`kwin-effect/`)

#### PlasmaZonesEffect (`plasmazoneseffect.cpp`)
C++ KWin effect plugin that detects window events and communicates via D-Bus.

**Key Features:**
- Polls for `window->isUserMove()` state (50ms interval)
- Tracks keyboard modifiers via `mouseChanged` signal
- Sends D-Bus signals: `dragStarted`, `dragMoved`, `dragStopped`
- Handles navigation: `moveWindowToZoneRequested`, `focusWindowInZoneRequested`

**D-Bus Communication:**
```cpp
void callDragStarted(const QString &windowId, const QRectF &geometry);
void callDragMoved(const QString &windowId, const QPointF &cursorPos, Qt::KeyboardModifiers mods);
void callDragStopped(KWin::EffectWindow *window, const QString &windowId);
```

**Autotiling Relevance:** Would need new D-Bus method `windowCreated(windowId, geometry)` to trigger autotiling on new windows.

---

### Settings (`src/config/settings.h`)

**Relevant Properties for Autotiling:**
```cpp
Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
Q_PROPERTY(int adjacentThreshold READ adjacentThreshold WRITE setAdjacentThreshold)
Q_PROPERTY(int minimumZoneSize READ minimumZoneSize WRITE setMinimumZoneSize)
Q_PROPERTY(int minimumZoneDisplaySize READ minimumZoneDisplaySize)
```

**New Settings Needed for Autotiling:**
- `autotilingEnabled` (bool)
- `autotilingMode` (enum: bsp, spiral, columns, rows, master-stack)
- `autotilingMasterRatio` (qreal, for master-stack layout)
- `autotilingGapSize` (int)

---

## D-Bus Interface Summary

From `dbus/org.plasmazones.xml` and `src/core/constants.h`:

| Interface | Purpose | Autotiling Use |
|-----------|---------|----------------|
| `org.plasmazones.WindowDrag` | Window drag events | Trigger recalculation |
| `org.plasmazones.WindowTracking` | Window-zone assignments | Track windows per zone |
| `org.plasmazones.ZoneDetection` | Zone queries | Find empty zones |
| `org.plasmazones.LayoutManager` | Layout CRUD | Modify autotile layouts |
| `org.plasmazones.Overlay` | Visual feedback | Show autotile preview |
| `org.plasmazones.Settings` | Configuration | Autotiling settings |

---

## Integration Points for Autotiling

### 1. AutotileService Class (NEW)

**Location:** `src/daemon/autotileservice.h`

**Responsibilities:**
- Track windows per screen
- Implement tiling algorithms (BSP, spiral, master-stack)
- Generate dynamic zone configurations
- Handle window add/remove events

**Suggested Interface:**
```cpp
class AutotileService : public QObject {
    Q_OBJECT
public:
    enum TilingMode { BSP, Spiral, Columns, Rows, MasterStack };

    void setEnabled(bool enabled);
    void setMode(TilingMode mode);
    void recalculateLayout(QScreen *screen);

public Q_SLOTS:
    void onWindowAdded(const QString &windowId, const QRect &geometry);
    void onWindowRemoved(const QString &windowId);
    void onWindowMoved(const QString &windowId, const QString &newZoneId);

Q_SIGNALS:
    void layoutChanged(QScreen *screen, Layout *newLayout);
};
```

### 2. Layout Modification Hook

**Location:** `src/core/layout.cpp`

**Integration:** Add method for dynamic zone modification:
```cpp
void Layout::setDynamicZones(const QList<QRectF> &relativeGeometries) {
    clearZones();
    for (const auto &geo : relativeGeometries) {
        addZone(geo);
    }
    Q_EMIT zonesChanged();
}
```

### 3. WindowTracking Enhancement

**Location:** `src/daemon/windowtrackingadaptor.cpp`

**Integration:** Emit signals that AutotileService can connect to:
```cpp
Q_SIGNALS:
    void windowCountChanged(QScreen *screen, int count);
    void windowListChanged(const QString &zoneId, const QStringList &windowIds);
```

### 4. KWin Effect Enhancement

**Location:** `kwin-effect/plasmazoneseffect.cpp`

**Integration:** Add window creation detection:
```cpp
void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow *w) {
    if (shouldAutoSnapWindow(w)) {
        // Notify daemon of new window for autotiling
        callWindowCreated(getWindowId(w), w->frameGeometry());
    }
}
```

### 5. D-Bus Interface Addition

**Location:** `dbus/org.plasmazones.xml`

**New Interface:**
```xml
<interface name="org.plasmazones.Autotile">
    <method name="setEnabled">
        <arg name="enabled" type="b" direction="in"/>
    </method>
    <method name="setMode">
        <arg name="mode" type="s" direction="in"/>
    </method>
    <method name="recalculate">
        <arg name="screenName" type="s" direction="in"/>
    </method>
    <signal name="layoutRecalculated">
        <arg name="screenName" type="s"/>
        <arg name="zoneCount" type="i"/>
    </signal>
</interface>
```

---

## Existing Reusable Code

### Geometry Utilities (`src/core/geometryutils.h`)
- `relativeToAbsolute(QRectF relative, QRect screen)` - Convert coordinates
- `absoluteToRelative(QRect absolute, QRect screen)` - Reverse conversion
- Zone intersection/containment checks

### Constants (`src/core/constants.h`)
- `Defaults::ZonePadding` (8px)
- `Defaults::MinimumZoneSizePx` (100px)
- `EditorConstants::MinZoneSize` (5% minimum)

### Screen Handling (`src/core/screenmanager.cpp`)
- Panel-aware geometry calculation
- Multi-monitor support
- Screen change detection

### Keyboard Navigation (KWin Effect)
- `queryAdjacentZone()` - Find neighboring zones
- `queryZoneGeometry()` - Get zone bounds
- Movement infrastructure already exists

---

## Architectural Recommendations

### 1. Separate Autotiling Layouts
Create a distinct layout type for autotiled screens:
```cpp
enum LayoutType {
    Static,      // User-defined fixed zones
    Autotile     // Dynamically calculated zones
};
```

### 2. Event-Driven Recalculation
Use Qt signals for lazy recalculation:
```cpp
// Only recalculate when needed
connect(windowTracker, &WindowTracker::windowCountChanged,
        autotileService, &AutotileService::scheduleRecalculation);

// Debounce rapid changes
QTimer::singleShot(100, this, &AutotileService::recalculateLayout);
```

### 3. Algorithm Abstraction
Create strategy pattern for tiling algorithms:
```cpp
class ITilingAlgorithm {
public:
    virtual QList<QRectF> calculateZones(int windowCount, qreal aspectRatio) = 0;
};

class BSPAlgorithm : public ITilingAlgorithm { ... };
class SpiralAlgorithm : public ITilingAlgorithm { ... };
class MasterStackAlgorithm : public ITilingAlgorithm { ... };
```

### 4. Preserve Manual Overrides
Allow users to "pin" windows to zones that autotiling respects:
```cpp
struct WindowState {
    QString windowId;
    QString pinnedZoneId;  // Empty = autotile manages
    bool floating;         // Excluded from autotiling
};
```

### 5. Animation Support
Leverage existing overlay for smooth transitions:
```cpp
// In OverlayService
void animateZoneTransition(Layout *from, Layout *to, int durationMs);
```

---

## Implementation Priority

| Priority | Component | Effort | Dependencies |
|----------|-----------|--------|--------------|
| 1 | AutotileService base class | Medium | None |
| 2 | BSP tiling algorithm | Medium | AutotileService |
| 3 | WindowTracking signals | Low | None |
| 4 | D-Bus interface | Low | AutotileService |
| 5 | Settings integration | Low | AutotileService |
| 6 | KWin effect windowAdded | Medium | D-Bus interface |
| 7 | Additional algorithms | Medium | BSP algorithm |
| 8 | Animation support | High | Overlay changes |

---

## Files to Modify

### New Files
- `src/daemon/autotileservice.h`
- `src/daemon/autotileservice.cpp`
- `src/daemon/autotileadaptor.h`
- `src/daemon/autotileadaptor.cpp`
- `src/core/tilingalgorithm.h` (interface)
- `src/core/bspalgorithm.cpp`
- `src/core/spiralalgorithm.cpp`
- `src/core/masterstackalgorithm.cpp`

### Modified Files
- `src/daemon/daemon.cpp` - Add AutotileService instantiation
- `src/daemon/CMakeLists.txt` - Add new sources
- `src/config/settings.h` - Add autotiling settings
- `src/daemon/windowtrackingadaptor.cpp` - Add count signals
- `kwin-effect/plasmazoneseffect.cpp` - Add windowCreated D-Bus call
- `dbus/org.plasmazones.xml` - Add Autotile interface
- `src/core/constants.h` - Add Autotile D-Bus interface constant

---

## Conclusion

The PlasmaZones codebase is well-architected for extension. The clean separation between KWin effect (event detection) and daemon (logic) makes autotiling a natural addition. The existing `Layout` and `Zone` classes can be reused directly, with the main work being the new `AutotileService` and tiling algorithm implementations.

Key advantages of the current architecture:
1. **D-Bus IPC** allows clean communication without tight coupling
2. **Relative geometry** system (0.0-1.0) simplifies multi-monitor support
3. **Interface-based design** enables easy testing and mocking
4. **Existing keyboard navigation** provides infrastructure for window movement
5. **Panel-aware geometry** in ScreenManager handles edge cases

Recommended first milestone: Implement BSP autotiling for a single screen with manual trigger via D-Bus, then expand to automatic window tracking.
