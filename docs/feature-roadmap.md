# PlasmaZones Feature Roadmap

Consolidated feature recommendations based on competitive analysis of KZones, MouseTiler, Krohnkite, and Bismuth.

**Date:** 2026-01-22

---

## Executive Summary

After analyzing four competing KDE window tiling solutions, we've identified **17 potential features** organized by priority. The highest-value additions focus on **keyboard navigation** - a gap that would attract users from the now-defunct Bismuth/Krohnkite ecosystem.

### Current Competitive Position
- ✅ Only actively maintained zone-based tiler with Plasma 5+6 support
- ✅ Only solution with WYSIWYG visual editor
- ✅ Most comprehensive feature set among active projects
- ✅ Full keyboard navigation (Phase 1 complete as of 2026-01-22)

---

## Phase 1: Keyboard Navigation ✅ COMPLETE

These features were requested across multiple competing projects and address the biggest gap in PlasmaZones.

**Completed:** 2026-01-22

### 1.1 Move Window to Adjacent Zone ✅ COMPLETE
**Sources:** KZones (#111, #101), Krohnkite, Bismuth
**User Value:** ⭐⭐⭐⭐⭐ (Most requested keyboard feature)

**Description:**
Move the active window to an adjacent zone using keyboard shortcuts.

**Current State:**
- ✅ `getAdjacentZone(currentZoneId, direction)` D-Bus method exists
- ✅ `adjacentZones()` detection in `Layout` class works
- ✅ Shortcuts registered: Meta+Alt+Left/Right/Up/Down
- ✅ D-Bus signal flow: ShortcutManager → WindowTrackingAdaptor → KWin Effect
- ✅ Navigation feedback signals for success/failure

**Implementation:**
```cpp
// Add to ShortcutManager
registerShortcut("moveWindowLeft", "Move Window Left", Qt::META | Qt::SHIFT | Qt::Key_Left);
registerShortcut("moveWindowRight", "Move Window Right", Qt::META | Qt::SHIFT | Qt::Key_Right);
registerShortcut("moveWindowUp", "Move Window Up", Qt::META | Qt::SHIFT | Qt::Key_Up);
registerShortcut("moveWindowDown", "Move Window Down", Qt::META | Qt::SHIFT | Qt::Key_Down);

// Logic in ZoneManager or new WindowMover class
void moveWindowToZone(Direction direction) {
    Window* focused = getFocusedWindow();
    QString currentZone = getWindowZone(focused);
    QString targetZone = getAdjacentZone(currentZone, direction);
    if (!targetZone.isEmpty()) {
        applyZoneGeometry(focused, targetZone);
    }
}
```

**Effort:** Low - Infrastructure exists, just needs shortcuts + glue code

---

### 1.2 Focus Navigation Between Zones ✅ COMPLETE
**Sources:** Krohnkite (Meta+H/J/K/L), Bismuth
**User Value:** ⭐⭐⭐⭐⭐ (Essential for keyboard-centric users)

**Description:**
Move keyboard focus to a window in an adjacent zone.

**Current State:**
- ✅ Adjacent zone detection exists
- ✅ `getWindowsInZone()` returns windows per zone
- ✅ Shortcuts registered: Meta+Ctrl+Left/Right/Up/Down
- ✅ D-Bus signal `focusWindowInZoneRequested` implemented
- ✅ Navigation feedback for no_window_in_zone, window_not_found, success

**Implementation:**
```cpp
// Shortcuts
registerShortcut("focusZoneLeft", "Focus Zone Left", Qt::META | Qt::Key_Left);
registerShortcut("focusZoneRight", "Focus Zone Right", Qt::META | Qt::Key_Right);
registerShortcut("focusZoneUp", "Focus Zone Up", Qt::META | Qt::Key_Up);
registerShortcut("focusZoneDown", "Focus Zone Down", Qt::META | Qt::Key_Down);

// Logic
void focusAdjacentZone(Direction direction) {
    Window* focused = getFocusedWindow();
    QString currentZone = getWindowZone(focused);
    QString targetZone = getAdjacentZone(currentZone, direction);
    QList<Window*> windows = getWindowsInZone(targetZone);
    if (!windows.isEmpty()) {
        windows.first()->activate();
    }
}
```

**Effort:** Low - Similar to window movement

---

### 1.3 Push Window to Empty Zone ✅ COMPLETE
**Sources:** KZones (#139, v1.0.0 milestone)
**User Value:** ⭐⭐⭐⭐ (Quick tiling without manual placement)

**Description:**
Automatically move a window to the first unoccupied zone.

**Current State:**
- ✅ Zone occupancy tracked via `getWindowsInZone()`
- ✅ `findEmptyZone()` method in WindowTrackingAdaptor
- ✅ Shortcut registered: Meta+Alt+P
- ✅ Navigation feedback for no_empty_zone, geometry_error, success

**Implementation:**
```cpp
registerShortcut("pushToEmptyZone", "Push to Empty Zone", Qt::META | Qt::Key_Return);

QString findEmptyZone() {
    Layout* layout = getCurrentLayout();
    for (const Zone& zone : layout->zones()) {
        if (getWindowsInZone(zone.id()).isEmpty()) {
            return zone.id();
        }
    }
    return QString(); // No empty zone
}

void pushWindowToEmptyZone() {
    QString emptyZone = findEmptyZone();
    if (!emptyZone.isEmpty()) {
        applyZoneGeometry(getFocusedWindow(), emptyZone);
    }
}
```

**Effort:** Low

---

### 1.4 Restore Window to Original Size ✅ COMPLETE
**Sources:** KZones (#159)
**User Value:** ⭐⭐⭐⭐ (Quick unsnap without dragging)

**Description:**
Keyboard shortcut to restore window to pre-snap dimensions.

**Current State:**
- ✅ `preSnapGeometry` stored for each window
- ✅ `getValidatedPreSnapGeometry()` retrieves it
- ✅ Shortcut registered: Meta+Alt+R
- ✅ D-Bus signal `restoreWindowRequested` implemented
- ✅ Navigation feedback for no_geometry, not_snapped, success

**Implementation:**
```cpp
registerShortcut("restoreWindowSize", "Restore Original Size", Qt::META | Qt::Key_Escape);

void restoreWindowSize() {
    Window* focused = getFocusedWindow();
    QRect original = getValidatedPreSnapGeometry(focused);
    if (original.isValid()) {
        focused->setGeometry(original);
        clearZoneAssignment(focused);
    }
}
```

**Effort:** Very Low - Just needs shortcut binding

---

### 1.5 Per-Window Float Toggle ✅ COMPLETE
**Sources:** Krohnkite (Meta+F), Bismuth
**User Value:** ⭐⭐⭐⭐ (Quick exclude without settings)

**Description:**
Toggle a window's exclusion from zone snapping with a shortcut.

**Current State:**
- ✅ Exclusion list exists (`excludedWindowClasses`)
- ✅ Per-window float tracking in `m_floatingWindows` (both daemon and effect)
- ✅ Shortcut registered: Meta+Alt+F
- ✅ D-Bus signal `toggleWindowFloatRequested` with shouldFloat parameter
- ✅ Float state sync between daemon and KWin effect
- ✅ Navigation feedback for floated/unfloated success

**Implementation:**
```cpp
QSet<Window*> temporarilyExcludedWindows;

registerShortcut("toggleWindowFloat", "Toggle Window Floating", Qt::META | Qt::Key_F);

void toggleWindowFloat() {
    Window* focused = getFocusedWindow();
    if (temporarilyExcludedWindows.contains(focused)) {
        temporarilyExcludedWindows.remove(focused);
        // Optionally snap to nearest zone
    } else {
        temporarilyExcludedWindows.insert(focused);
        restoreWindowSize(); // Unsnap
    }
}

bool isWindowExcluded(Window* w) {
    return temporarilyExcludedWindows.contains(w) ||
           isInExcludeList(w->windowClass());
}
```

**Effort:** Low

---

## Phase 2: App Filtering & Automation (Medium Priority)

### 2.1 Include Mode for App Filtering
**Sources:** KZones (#158)
**User Value:** ⭐⭐⭐⭐ (Whitelist approach for specific workflows)

**Description:**
Add whitelist mode where only specified apps participate in zone snapping.

**Current State:**
- ✅ `excludedWindowClasses` (blacklist) exists
- ❌ No include mode (whitelist)

**Implementation:**
```cpp
// Settings
QString filterMode; // "exclude" (default) or "include"
QStringList includedWindowClasses;

bool isWindowExcluded(Window* w) {
    if (filterMode == "include") {
        return !matchesPatternList(w->windowClass(), includedWindowClasses);
    } else {
        return matchesPatternList(w->windowClass(), excludedWindowClasses);
    }
}
```

**UI Changes:**
- Add radio buttons: "Exclude listed apps" / "Only include listed apps"
- Rename field contextually

**Effort:** Low

---

### 2.2 Cycle Windows in Same Zone
**Sources:** KZones (#111)
**User Value:** ⭐⭐⭐ (Useful when stacking windows in zones)

**Description:**
Keyboard shortcut to cycle focus between windows in the same zone.

**Current State:**
- ✅ `getWindowsInZone(zoneId)` returns window list
- ❌ No cycling logic

**Implementation:**
```cpp
registerShortcut("cycleWindowsInZone", "Cycle Windows in Zone", Qt::META | Qt::Key_Tab);

void cycleWindowsInZone() {
    Window* focused = getFocusedWindow();
    QString zone = getWindowZone(focused);
    QList<Window*> windows = getWindowsInZone(zone);

    int currentIndex = windows.indexOf(focused);
    int nextIndex = (currentIndex + 1) % windows.size();
    windows[nextIndex]->activate();
}
```

**Effort:** Very Low

---

### 2.3 Per-Zone Application Targeting
**Sources:** KZones
**User Value:** ⭐⭐⭐ (Auto-placement for specific apps)

**Description:**
Configure applications to automatically snap to specific zones when opened.

**Current State:**
- ✅ "Move new windows to last used zone" exists
- ❌ No per-zone app assignment

**Implementation:**
```cpp
// Zone property in layout JSON
{
    "id": "zone1",
    "geometry": {...},
    "targetApplications": ["firefox", "code", "konsole"]
}

void onWindowCreated(Window* w) {
    for (const Zone& zone : getCurrentLayout()->zones()) {
        if (zone.targetApplications().contains(w->appId())) {
            applyZoneGeometry(w, zone.id());
            return;
        }
    }
    // Fall back to last-used zone behavior
}
```

**UI Changes:**
- Add "Target Applications" field in zone editor
- Comma-separated app names or pattern matching

**Effort:** Medium

---

## Phase 3: Visual Enhancements (Medium Priority)

### 3.1 Window Transparency During Drag
**Sources:** KZones
**User Value:** ⭐⭐⭐ (Better zone visibility)

**Description:**
Apply transparency to dragged window for better zone visibility.

**Current State:**
- ❌ No visual effect on dragged window
- ✅ KWin effect has access to window properties

**Implementation:**
```cpp
// In PlasmaZonesEffect
void onDragStarted(Window* w) {
    m_originalOpacity = w->opacity();
    w->setOpacity(m_dragOpacity); // Default 0.7
}

void onDragEnded(Window* w) {
    w->setOpacity(m_originalOpacity);
}
```

**Settings:**
- `dragWindowOpacity`: 0.1 - 1.0, default 0.7
- `enableDragTransparency`: bool, default true

**Effort:** Low

---

### 3.2 Zone Selector with Layout Preview
**Sources:** KZones (#104)
**User Value:** ⭐⭐⭐ (Faster layout switching)

**Description:**
Enhance zone selector to show layout thumbnails and allow switching.

**Current State:**
- ✅ `ZoneSelectorController` shows current layout zones
- ❌ No layout switching in selector

**Implementation:**
- Add layout list/thumbnails to zone selector UI
- Hover to preview layout zones
- Click layout to switch, then click zone to snap

**Effort:** Medium (UI work)

---

### 3.3 Center-in-Zone Option
**Sources:** MouseTiler
**User Value:** ⭐⭐⭐ (Specific workflow needs)

**Description:**
Option to center windows within zones instead of filling completely.

**Current State:**
- ❌ Windows always fill zone geometry

**Implementation:**
```cpp
// Zone property
enum FillMode { Fill, Center, Contain };

QRect calculateWindowGeometry(const Zone& zone, Window* w, FillMode mode) {
    if (mode == Fill) {
        return zone.geometry();
    } else if (mode == Center) {
        QSize windowSize = w->preferredSize();
        QRect zoneRect = zone.geometry();
        int x = zoneRect.x() + (zoneRect.width() - windowSize.width()) / 2;
        int y = zoneRect.y() + (zoneRect.height() - windowSize.height()) / 2;
        return QRect(QPoint(x, y), windowSize);
    }
    // Contain: scale to fit while maintaining aspect ratio
}
```

**UI Changes:**
- Add "Fill Mode" dropdown in zone editor: Fill / Center / Contain

**Effort:** Medium

---

### 3.4 Global Window Gaps Setting
**Sources:** Bismuth
**User Value:** ⭐⭐⭐ (Consistent spacing)

**Description:**
Global gap setting in addition to per-zone margins.

**Current State:**
- ✅ Per-zone margins in editor

**Enhancement:**
- Add global `windowGap` setting (applies to all zones)
- Per-zone margins override global if set
- Runtime gap adjustment shortcut (increase/decrease)

**Effort:** Low

---

## Phase 4: Advanced Features (Lower Priority)

### 4.1 Overlapping Zone Disambiguation
**Sources:** KZones (#105)
**User Value:** ⭐⭐ (Edge case handling)

**Description:**
UI to select specific zone when multiple zones overlap at cursor position.

**Implementation:**
- When cursor is in overlapping area, show small popup with zone options
- Or use keyboard modifier (Shift) to cycle through overlapping zones

**Effort:** Medium

---

### 4.2 OSD Display Options
**Sources:** KZones (#181)
**User Value:** ⭐⭐ (Preference customization)

**Description:**
Option to only show OSD during window drag, not on layout switch.

**Implementation:**
```cpp
bool showOsdOnLayoutSwitch; // default true
bool showOsdDuringDrag;     // default true

void onLayoutSwitch() {
    if (showOsdOnLayoutSwitch) {
        showOsd(newLayout->name());
    }
}
```

**Effort:** Very Low

---

### 4.3 Preset Algorithm Layouts
**Sources:** Krohnkite, Bismuth
**User Value:** ⭐⭐ (Attract dynamic tiling users)

**Description:**
Offer popular tiling layouts as presets in layout manager.

**Presets:**
| Name | Description |
|------|-------------|
| Master + Stack | 60% left, 40% right stacked |
| Three Column | 25% / 50% / 25% |
| Fibonacci | Spiral subdivision |
| Wide | Large top, row below |
| BSP-style | 4-zone recursive split |
| Quarters | 4 equal quadrants |

**Effort:** Low (just preset definitions)

---

### 4.4 Dynamic Zone Resizing
**Sources:** KZones (#163)
**User Value:** ⭐⭐ (Power user feature)

**Description:**
Zones expand when neighbors are empty.

**Complexity:** High - requires:
- Real-time occupancy monitoring
- Dynamic geometry recalculation
- Animation of boundaries
- Rules for expansion behavior

**Recommendation:** Defer unless significant demand

**Effort:** High

---

### 4.5 Runtime Zone Boundary Adjustment
**Sources:** KZones (#134, v1.0.0 milestone)
**User Value:** ⭐⭐ (PowerToys parity)

**Description:**
Drag zone boundaries at runtime, affecting all windows in zones.

**Complexity:** High - requires:
- Runtime boundary manipulation UI
- Multi-window geometry updates
- Persisting adjusted boundaries

**Effort:** High

---

## Implementation Roadmap

### v1.1 - Keyboard Navigation Release
**Timeline:** Next release
**Features:**
1. ✅ Move window to adjacent zone (1.1)
2. ✅ Focus navigation between zones (1.2)
3. ✅ Push window to empty zone (1.3)
4. ✅ Restore window to original size (1.4)
5. ✅ Per-window float toggle (1.5)

**Impact:** Major usability improvement for keyboard users

---

### v1.2 - App Management Release
**Features:**
1. ✅ Include mode for app filtering (2.1)
2. ✅ Cycle windows in same zone (2.2)
3. ✅ Window transparency during drag (3.1)
4. ✅ OSD display options (4.2)

---

### v1.3 - Visual Polish Release
**Features:**
1. ✅ Zone selector with layout preview (3.2)
2. ✅ Global window gaps setting (3.4)
3. ✅ Preset algorithm layouts (4.3)

---

### Future Considerations
- Per-zone application targeting (2.3)
- Center-in-zone option (3.3)
- Overlapping zone disambiguation (4.1)
- Dynamic zone resizing (4.4) - if demand
- Runtime boundary adjustment (4.5) - if demand

---

## Summary

| Priority | Features | Effort | Impact | Status |
|----------|----------|--------|--------|--------|
| **Phase 1** | 5 keyboard features | Low | Very High | ✅ Complete |
| **Phase 2** | 3 app/automation features | Low-Medium | High | Pending |
| **Phase 3** | 4 visual features | Low-Medium | Medium | Pending |
| **Phase 4** | 5 advanced features | Medium-High | Low-Medium | Pending |

**Next Focus:** Phase 2 app management features (include mode, cycle windows, drag transparency) would further enhance the user experience.

---

## References

- [KZones Comparison](./feature-comparison-kzones.md)
- [MouseTiler Comparison](./feature-comparison-mousetiler.md)
- [Krohnkite Comparison](./feature-comparison-krohnkite.md)
- [Bismuth Comparison](./feature-comparison-bismuth.md)
