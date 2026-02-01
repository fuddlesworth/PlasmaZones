# PlasmaZones OSD Enhancement Design Document

## Document Info
- **Status**: Draft
- **Created**: 2026-01-31
- **Author**: Claude (AI Assistant)
- **Related Files**:
  - `src/ui/LayoutOsd.qml`
  - `src/ui/NavigationOsd.qml`
  - `src/shared/ZonePreview.qml`
  - `src/daemon/overlayservice.cpp`
  - `src/config/plasmazones.kcfg`
  - `src/config/configdefaults.h`
  - `src/config/interfaces.h`
  - `src/config/settings.h`
  - `src/config/settings.cpp`

---

## 1. Executive Summary

The current OSD (On-Screen Display) system in PlasmaZones provides minimal visual feedback during layout switches and window navigation. While functional, the OSDs underutilize their screen presence by showing only zone geometry without contextual information about window state, available actions, or guidance for users.

This document proposes a phased enhancement plan to transform the OSD from a passive preview into an informative, optionally interactive overlay that improves discoverability and workflow efficiency.

---

## 2. Problem Statement

### Current Limitations

| Issue | Impact |
|-------|--------|
| **No window context** | Users can't see which windows are in which zones |
| **No guidance** | New users don't learn shortcuts from the OSD |
| **No state awareness** | Can't tell which zone is active or has focus |
| **Stacked windows invisible** | No indication when zones contain multiple windows |

**Note:** Interactive zone selection is handled by the Zone Selector (shown during drag).
The OSD is purely for visual feedback after actions.

### User Pain Points

1. "I switched layouts but can't remember what's where now"
2. "I see the layout but don't know the shortcuts to use it"
3. "I can't tell if my window actually moved"
4. "The OSD blocks what I'm trying to look at"

---

## 3. Design Goals

1. **Informative**: Show window state, not just zone geometry
2. **Educational**: Teach shortcuts through contextual hints
3. **Responsive**: Reflect actual window positions in real-time
4. **Accessible**: Screen reader support, high contrast, keyboard dismissal
5. **Performant**: No perceptible lag; smooth animations

**Note:** Interactive zone selection is handled by the Zone Selector component.
The OSD focuses purely on visual feedback.

---

## 4. Proposed Enhancements

### 4.1 Phase 1: Information Enhancement (Low Effort)

#### 4.1.1 Window Count Badges

Show the number of windows in each zone when count > 1.

```
Current:                    Enhanced:
┌─────────┬─────────┐      ┌─────────┬─────────┐
│    1    │    2    │      │    1    │   2 ⬡3  │  ← "3" badge
├─────────┼─────────┤      ├─────────┼─────────┤
│    3    │    4    │      │    3    │    4    │
└─────────┴─────────┘      └─────────┴─────────┘
```

**Implementation:**
- Add `windowCounts` property to ZonePreview (array of integers)
- Render badge in top-right corner of zone when count > 1
- Use `Kirigami.Theme.highlightColor` for badge background

#### 4.1.2 Active Zone Highlight

Highlight the zone containing the currently focused window.

```
┌─────────┬─────────┐
│    1    │ ██ 2 ██ │  ← Active zone has bold border
├─────────┼─────────┤     or filled highlight
│    3    │    4    │
└─────────┴─────────┘
```

**Implementation:**
- Add `activeZoneIndex` property to ZonePreview
- Apply `Kirigami.Theme.highlightColor` fill with 30% opacity
- Add 2px highlight-colored border to active zone

#### 4.1.3 Zone Shortcut Labels

Show the keyboard shortcut for each zone (Meta+1, Meta+2, etc.).

```
┌─────────┬─────────┐
│  ⌘1     │  ⌘2     │  ← Small shortcut hint
│         │         │
├─────────┼─────────┤
│  ⌘3     │  ⌘4     │
└─────────┴─────────┘
```

**Implementation:**
- Add `showShortcuts` boolean property (default: true, configurable)
- Render in bottom-left corner of each zone
- Use smaller font size (Kirigami.Units.smallFont)
- Semi-transparent text (60% opacity)

#### 4.1.4 Contextual Action Hints

Show a relevant hint based on the current action or state.

```
┌─────────────────────────────┐
│   [Zone Preview Grid]       │
│                             │
│   Layout Name               │
│   ─────────────────────────│
│   Tip: Meta+← to undo move  │  ← Contextual hint
└─────────────────────────────┘
```

**Hint Examples:**
| Context | Hint (must use i18n) |
|---------|------|
| After move | `i18n("Meta+← to undo")` or `i18n("Meta+Z to cycle")` |
| Layout switch | `i18n("Meta+[1-9] to snap windows")` |
| Autotile active | `i18n("Drag windows to auto-arrange")` |
| Failed navigation | `i18n("Try Meta+Q to push to empty zone")` |
| Stacked zone | `i18n("Meta+Z to cycle through windows")` |

**IMPORTANT: i18n Requirement**
All user-visible hint strings MUST be wrapped in `i18n()` per .cursorrules. The hints are
generated in C++ (OverlayService) and must use `i18n()` from `KLocalizedString`.

**Implementation:**
- Add `contextHint` string property to OSD QML
- OverlayService determines hint based on action/state using `i18n()`:
  ```cpp
  QString OverlayService::determineContextHint(const QString &action, bool success) {
      if (action == QLatin1String("move") && success) {
          return i18n("Meta+← to undo");
      }
      // ... etc
  }
  ```
- Render below layout name with `Kirigami.Theme.disabledTextColor`

---

### 4.2 Phase 2: Rich Window Context (Medium Effort)

#### 4.2.1 Window Titles in Zones

Show truncated window titles in each zone.

```
┌──────────────┬──────────────┐
│ 1            │ 2            │
│ Firefox      │ VS Code      │
│              │ Terminal...  │  ← Truncated if stacked
├──────────────┼──────────────┤
│ 3            │ 4            │
│ Dolphin      │ (empty)      │
└──────────────┴──────────────┘
```

**Implementation:**
- Add `zoneWindows` property: `[{zoneIndex, title, appId, isActive}]`
- WindowTracker provides window-to-zone mapping
- Truncate with ellipsis if title > zone width
- Show "(empty)" or leave blank for empty zones
- Stack titles vertically if multiple windows (max 2-3 visible)

#### 4.2.2 Window Thumbnails

Show miniature window previews in each zone.

```
┌──────────────┬──────────────┐
│ ┌──────────┐ │ ┌──────────┐ │
│ │ Firefox  │ │ │ VS Code  │ │
│ │ [thumb]  │ │ │ [thumb]  │ │
│ └──────────┘ │ └──────────┘ │
├──────────────┼──────────────┤
│ ┌──────────┐ │              │
│ │ Dolphin  │ │   (empty)    │
│ └──────────┘ │              │
└──────────────┴──────────────┘
```

**Implementation:**
- Use KWin's window thumbnail API (`PlasmaCore.WindowThumbnail`)
- Scale thumbnails to fit zone preview area
- Cache thumbnails to avoid re-rendering on every show
- Add setting to enable/disable (performance consideration)
- Fallback to app icon if thumbnail unavailable

---

### 4.3 Phase 3: Advanced Features (Higher Effort)

#### 4.3.1 Animated Window Movement Preview

Show an animation of where the window is moving.

```
Frame 1:              Frame 2:              Frame 3:
┌─────┬─────┐        ┌─────┬─────┐        ┌─────┬─────┐
│[win]│     │   →    │ [w] │     │   →    │     │[win]│
├─────┼─────┤        ├──→──┼─────┤        ├─────┼─────┤
│     │     │        │     │     │        │     │     │
└─────┴─────┘        └─────┴─────┘        └─────┴─────┘
```

**Implementation:**
- Add `animateFrom` and `animateTo` zone indices
- Animate a "ghost" rectangle from source to destination
- Use spring animation for natural feel
- Duration: 200-300ms
- Only for move/swap/push actions

#### 4.3.2 Multi-Monitor Layout Overview

Show layouts across all monitors in a unified view.

```
┌─────────────────────────────────────────────────┐
│  Monitor 1              │  Monitor 2            │
│  ┌─────┬─────┐         │  ┌───────────────┐   │
│  │  1  │  2  │         │  │       1       │   │
│  ├─────┼─────┤         │  ├───────┬───────┤   │
│  │  3  │  4  │         │  │   2   │   3   │   │
│  └─────┴─────┘         │  └───────┴───────┘   │
│  "Quad Layout"          │  "Wide + Stack"      │
└─────────────────────────────────────────────────┘
```

**Implementation:**
- New OSD type: `GlobalLayoutOsd.qml`
- Triggered by new shortcut (e.g., Meta+Shift+L)
- Shows all monitors with their current layouts
- Clickable to switch focus to monitor
- Useful for multi-monitor setups

---

## 5. Visual Design Specifications

### 5.1 Layout OSD (Enhanced)

```
┌─────────────────────────────────────────┐
│                                         │
│   ┌──────────┬──────────┬──────────┐   │
│   │ ⌘1       │ ⌘2    ⬡2 │          │   │
│   │ Firefox  │ VS Code  │          │   │
│   │ [thumb]  │ Terminal │    ⌘3    │   │
│   ├──────────┴──────────┤  Dolphin │   │
│   │        ⌘4           │  [thumb] │   │
│   │      (empty)        │          │   │
│   └─────────────────────┴──────────┘   │
│                                         │
│   [Auto] Master-Stack                   │
│   ───────────────────────────────────   │
│   Meta+[1-4] to snap • Meta+Z to cycle  │
│                                         │
└─────────────────────────────────────────┘

Legend:
  ⌘1       = Zone shortcut (bottom-left, subtle)
  ⬡2       = Window count badge (top-right)
  [thumb]  = Window thumbnail (optional)
  Firefox  = Window title (truncated)
  [Auto]   = Category badge
  ───────  = Separator line
  Tip line = Contextual hint
```

### 5.2 Navigation OSD (Enhanced)

```
Success State:
┌────────────────────────────────┐
│  ┌────────┬────────┐          │
│  │   1    │ → 2 ←  │          │  ← Destination highlighted
│  ├────────┼────────┤          │
│  │   3    │   4    │          │
│  └────────┴────────┘          │
│                                │
│  ✓ Moved to Zone 2            │
│  ─────────────────────────────│
│  Meta+← to undo               │
└────────────────────────────────┘

Failure State:
┌────────────────────────────────┐
│                                │
│   ✗ No zone to the right      │
│   ─────────────────────────── │
│   Try Meta+Q to push elsewhere │
│                                │
└────────────────────────────────┘
```

---

## 6. Technical Implementation

### 6.1 Data Flow

```
                    ┌─────────────────────────┐
                    │   WindowTracker         │
                    │   - windowZoneMap       │
                    │   - zoneCounts          │
                    │   - activeWindowZone    │
                    └───────────┬─────────────┘
                                │
                    ┌───────────▼─────────────┐
                    │   OverlayService        │
                    │   - buildZoneContext()  │
                    │   - determineHint()     │
                    └───────────┬─────────────┘
                                │
                    ┌───────────▼─────────────┐
                    │   LayoutOsd.qml         │
                    │   Properties:           │
                    │   - zoneWindows[]       │
                    │   - activeZoneIndex     │
                    │   - windowCounts[]      │
                    │   - contextHint         │
                    └─────────────────────────┘
```

### 6.2 New/Modified Components

#### ZonePreview.qml Changes

```qml
// New properties (typed where possible per .cursorrules)
property var windowCounts: []           // [0, 2, 1, 3] - windows per zone
property int activeZoneIndex: -1        // Currently focused zone
property bool showShortcuts: true       // Show Meta+N hints
property bool showThumbnails: false     // Show window previews
property var zoneWindows: []            // [{zone, title, windowId, isActive}]

// Accessibility (required per .cursorrules)
Accessible.role: Accessible.Graphic
Accessible.name: i18n("Zone layout preview")
```

**Accessibility Requirements for Zone Items:**
```qml
// Each zone Rectangle in the Repeater must have:
Accessible.role: Accessible.Graphic
Accessible.name: {
    var count = windowCounts[zoneIndex] || 0
    if (count === 0) return i18n("Zone %1, empty", zoneIndex + 1)
    if (count === 1) return i18n("Zone %1, 1 window", zoneIndex + 1)
    return i18n("Zone %1, %2 windows", zoneIndex + 1, count)
}
```

#### OverlayService.cpp Changes

```cpp
// New methods (using Qt6 string literal conventions per .cursorrules)
QVariantMap buildZoneContext(Layout* layout);
QString determineContextHint(const QString& action, bool success);

// Enhanced showLayoutOsd
void showLayoutOsd(Layout* layout, const QVariantMap& context);

// New window context struct
struct ZoneWindowInfo {
    int zoneIndex;
    QString title;
    QString appId;
    WId windowId;
    bool isActive;
};

// Example implementation with proper Qt6 string handling:
QString OverlayService::determineContextHint(const QString& action, bool success)
{
    // Use QLatin1String for comparisons per .cursorrules
    if (action == QLatin1String("move")) {
        return success ? i18n("Meta+← to undo") : i18n("Try another direction");
    }
    if (action == QLatin1String("swap")) {
        return success ? i18n("Windows swapped") : i18n("No adjacent window");
    }
    if (action == QLatin1String("push")) {
        return success ? i18n("Window pushed") : i18n("No empty zone available");
    }
    if (action == QLatin1String("layout")) {
        return i18n("Meta+[1-9] to snap windows");
    }
    return QString();
}

// buildZoneContext using QLatin1String for QVariantMap keys
QVariantMap OverlayService::buildZoneContext(Layout* layout)
{
    QVariantMap context;
    context[QLatin1String("windowCounts")] = getWindowCountsPerZone(layout);
    context[QLatin1String("activeZoneIndex")] = getActiveWindowZoneIndex();
    context[QLatin1String("zoneWindows")] = getZoneWindowInfoList(layout);
    return context;
}
```

#### New Settings (Complete KConfigXT Workflow)

**IMPORTANT**: Per .cursorrules, new settings MUST follow the complete workflow:
kcfg → configdefaults.h → interfaces.h → settings.h → settings.cpp

**Step 1: plasmazones.kcfg** (Single Source of Truth)
```xml
<group name="Display">
  <entry name="ShowWindowTitlesInOsd" type="Bool">
    <label>Show window titles in OSD zone previews</label>
    <default>true</default>
  </entry>
  <entry name="ShowWindowThumbnailsInOsd" type="Bool">
    <label>Show window thumbnails in OSD (may affect performance)</label>
    <default>false</default>
  </entry>
  <entry name="ShowZoneShortcutsInOsd" type="Bool">
    <label>Show zone keyboard shortcuts in OSD</label>
    <default>true</default>
  </entry>
  <entry name="ShowContextHintsInOsd" type="Bool">
    <label>Show contextual action hints in OSD</label>
    <default>true</default>
  </entry>
  <entry name="OsdWindowCountThreshold" type="Int">
    <label>Minimum window count to show badge</label>
    <default>1</default>
    <min>1</min>
    <max>10</max>
  </entry>
</group>
```

**Step 2: configdefaults.h**
```cpp
// Add static accessors for each new setting
static bool showWindowTitlesInOsd() { return instance().defaultShowWindowTitlesInOsdValue(); }
static bool showWindowThumbnailsInOsd() { return instance().defaultShowWindowThumbnailsInOsdValue(); }
static bool showZoneShortcutsInOsd() { return instance().defaultShowZoneShortcutsInOsdValue(); }
static bool showContextHintsInOsd() { return instance().defaultShowContextHintsInOsdValue(); }
static int osdWindowCountThreshold() { return instance().defaultOsdWindowCountThresholdValue(); }
```

**Step 3: interfaces.h** (ISettings interface)
```cpp
Q_SIGNALS:
    void showWindowTitlesInOsdChanged();
    void showWindowThumbnailsInOsdChanged();
    void showZoneShortcutsInOsdChanged();
    void showContextHintsInOsdChanged();
    void osdWindowCountThresholdChanged();
```

**Step 4: settings.h**
```cpp
// Q_PROPERTY declarations
Q_PROPERTY(bool showWindowTitlesInOsd READ showWindowTitlesInOsd
           WRITE setShowWindowTitlesInOsd NOTIFY showWindowTitlesInOsdChanged)
Q_PROPERTY(bool showWindowThumbnailsInOsd READ showWindowThumbnailsInOsd
           WRITE setShowWindowThumbnailsInOsd NOTIFY showWindowThumbnailsInOsdChanged)
// ... etc for all 7 settings

// Getters (inline)
bool showWindowTitlesInOsd() const { return m_showWindowTitlesInOsd; }
// ... etc

// Setters
void setShowWindowTitlesInOsd(bool value);
// ... etc

// Member variables (with fallback defaults matching kcfg)
bool m_showWindowTitlesInOsd = true;
bool m_showWindowThumbnailsInOsd = false;
bool m_showZoneShortcutsInOsd = true;
bool m_showContextHintsInOsd = true;
int m_osdWindowCountThreshold = 1;
```

**Step 5: settings.cpp**
```cpp
// Setter pattern (example for one setting)
void Settings::setShowWindowTitlesInOsd(bool value) {
    if (m_showWindowTitlesInOsd != value) {
        m_showWindowTitlesInOsd = value;
        Q_EMIT showWindowTitlesInOsdChanged();
        Q_EMIT settingsChanged();
    }
}

// In load() - use ConfigDefaults
KConfigGroup displayGroup = config->group(QStringLiteral("Display"));
m_showWindowTitlesInOsd = displayGroup.readEntry(
    QLatin1String("ShowWindowTitlesInOsd"),
    ConfigDefaults::showWindowTitlesInOsd());
// ... etc for all settings

// In save()
KConfigGroup displayGroup = config->group(QStringLiteral("Display"));
displayGroup.writeEntry(QLatin1String("ShowWindowTitlesInOsd"), m_showWindowTitlesInOsd);
// ... etc

// In reset() - use ConfigDefaults
m_showWindowTitlesInOsd = ConfigDefaults::showWindowTitlesInOsd();
// ... etc
```

### 6.3 Performance Considerations

| Feature | Impact | Mitigation |
|---------|--------|------------|
| Window thumbnails | High - compositor overhead | Cache thumbnails; lazy load; make optional |
| Window titles | Low - string queries | Cache window-zone mapping |
| Smart positioning | Low - geometry calculations | Calculate once per show |
| Hover animations | Low - GPU accelerated | Use QML animations |
| Click handlers | Negligible | Standard Qt event handling |

### 6.4 Accessibility

| Feature | Implementation |
|---------|----------------|
| Screen reader | Add `Accessible.name` and `Accessible.role` to all elements |
| High contrast | Respect `Kirigami.Theme` colors exclusively (no hardcoded colors) |
| Keyboard dismiss | Escape key auto-dismisses OSD |
| Reduced motion | Check `Kirigami.Units.longDuration` for animation prefs |

**Required Accessible Properties per .cursorrules:**
```qml
// Zone preview container:
Accessible.role: Accessible.Graphic
Accessible.name: i18n("Layout preview showing %1 zones", zones.length)

// Each zone:
Accessible.role: Accessible.Graphic
Accessible.name: i18n("Zone %1, %2 windows", index + 1, windowCount)

// Action hints:
Accessible.role: Accessible.StaticText
Accessible.name: contextHint  // Already i18n wrapped from C++
```

### 6.5 CMake Registration (CRITICAL)

**Per .cursorrules: ALL QML files MUST be registered in CMakeLists.txt**

If any new QML components are created, they MUST be added to the appropriate
`qt_add_qml_module()` call. Missing registration causes "X is not a type" runtime errors.

```cmake
# In src/CMakeLists.txt - if GlobalLayoutOsd.qml is created:
qt_add_qml_module(plasmazones-daemon
    URI org.plasmazones.daemon
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/LayoutOsd.qml
        ui/NavigationOsd.qml
        ui/GlobalLayoutOsd.qml    # <-- Add new files here!
        shared/ZonePreview.qml
        # ... other files
)
```

**Checklist for new QML files:**
- [ ] Add to `QML_FILES` in CMakeLists.txt
- [ ] Rebuild to verify registration
- [ ] Test import in another QML file

---

## 7. Configuration UI (KCM)

Add new section to PlasmaZones KCM:

```
┌─────────────────────────────────────────────────────┐
│ On-Screen Display                                   │
├─────────────────────────────────────────────────────┤
│                                                     │
│ Layout OSD                                          │
│ ┌─────────────────────────────────────────────────┐│
│ │ ☑ Show OSD when switching layouts               ││
│ │ ☑ Show window titles in zones                   ││
│ │ ☐ Show window thumbnails (may affect performance)││
│ │ ☑ Show zone shortcuts (Meta+1, Meta+2, etc.)    ││
│ │ ☑ Show contextual hints                         ││
│ │                                                 ││
│ │ Display duration: [====●=====] 1.5s             ││
│ └─────────────────────────────────────────────────┘│
│                                                     │
│ Navigation OSD                                      │
│ ┌─────────────────────────────────────────────────┐│
│ │ ☑ Show OSD for navigation actions               ││
│ │ ☑ Show zone preview on success                  ││
│ │                                                 ││
│ │ Display duration: [===●======] 1.0s             ││
│ └─────────────────────────────────────────────────┘│
│                                                     │
└─────────────────────────────────────────────────────┘
```

---

## 8. Implementation Roadmap

### Phase 1: Information Enhancement
**Estimated Scope: Small**

| Task | Files Modified |
|------|----------------|
| Add windowCounts to ZonePreview | `src/shared/ZonePreview.qml` |
| Add activeZoneIndex highlighting | `src/shared/ZonePreview.qml` |
| Add zone shortcut labels | `src/shared/ZonePreview.qml` |
| Add Accessible properties | `src/shared/ZonePreview.qml`, `src/ui/LayoutOsd.qml` |
| Add contextHint to LayoutOsd | `src/ui/LayoutOsd.qml` |
| Add hint generation logic (with i18n) | `src/daemon/overlayservice.cpp` |
| **Settings - Step 1** | `src/config/plasmazones.kcfg` |
| **Settings - Step 2** | `src/config/configdefaults.h` |
| **Settings - Step 3** | `src/config/interfaces.h` |
| **Settings - Step 4** | `src/config/settings.h` |
| **Settings - Step 5** | `src/config/settings.cpp` |
| Update KCM UI | `kcm/contents/ui/main.qml` |

**Dependencies:** None

**Settings to add in Phase 1:**
- `ShowZoneShortcutsInOsd` (bool, default: true)
- `ShowContextHintsInOsd` (bool, default: true)
- `OsdWindowCountThreshold` (int, default: 1)

### Phase 2: Rich Window Context
**Estimated Scope: Medium**

| Task | Files Modified |
|------|----------------|
| Wire windowCounts from WindowTracker | `src/daemon/overlayservice.cpp` |
| Wire activeWindowZoneIndex from WindowTracker | `src/daemon/overlayservice.cpp` |
| Add window-to-zone tracking | `src/daemon/windowtracker.cpp`, `src/daemon/windowtracker.h` |
| Build zoneWindows context | `src/daemon/overlayservice.cpp`, `src/daemon/overlayservice.h` |
| Add window titles to ZonePreview | `src/shared/ZonePreview.qml` |
| Add window thumbnails (optional) | `src/shared/ZonePreview.qml` |
| **Settings - Phase 2** (kcfg workflow) | All 5 config files |
| Update KCM UI for new settings | `kcm/contents/ui/main.qml` |

**Dependencies:** Phase 1 complete

**Settings to add in Phase 2:**
- `ShowWindowTitlesInOsd` (bool, default: true)
- `ShowWindowThumbnailsInOsd` (bool, default: false)

### Phase 3: Advanced Features
**Estimated Scope: Medium**

| Task | Files Modified |
|------|----------------|
| Animated movement preview | `src/ui/NavigationOsd.qml` |
| Multi-monitor overview | **New:** `src/ui/GlobalLayoutOsd.qml` |
| **Register new QML in CMake** | `src/CMakeLists.txt` |

**Dependencies:** Phase 2 complete

**CRITICAL for Phase 3:**
If `GlobalLayoutOsd.qml` is created, it MUST be registered in CMakeLists.txt:
```cmake
QML_FILES
    ui/GlobalLayoutOsd.qml  # Add this line
```

---

## 9. Implementation Checklist (.cursorrules Compliance)

Before merging any OSD enhancement PR, verify:

### QML Compliance
- [ ] All user-visible strings use `i18n()` or `i18nc()`
- [ ] All colors use `Kirigami.Theme.*` (no hardcoded hex values)
- [ ] All spacing uses `Kirigami.Units.*` (no hardcoded pixels except in comments)
- [ ] All interactive elements have `Accessible.role` and `Accessible.name`
- [ ] New QML files registered in CMakeLists.txt `QML_FILES`
- [ ] Properties use typed declarations where possible (not just `var`)
- [ ] Signals use past-tense naming (`zoneClicked`, not `clickZone`)

### C++ Compliance
- [ ] JSON keys use `QLatin1String()` not raw strings
- [ ] String comparisons use `QLatin1String()` not raw strings
- [ ] Constants use `QStringLiteral()` for MIME types and paths
- [ ] New settings follow complete kcfg workflow (5 files)
- [ ] Signals only emit when value actually changes
- [ ] Use `Q_EMIT` macro not bare `emit`
- [ ] Include SPDX headers in new files

### Settings Compliance
- [ ] Entry added to `plasmazones.kcfg` with `<label>` and `<default>`
- [ ] Static accessor added to `configdefaults.h`
- [ ] Signal added to `interfaces.h` ISettings interface
- [ ] Q_PROPERTY, getter, setter, member added to `settings.h`
- [ ] Setter, load(), save(), reset() updated in `settings.cpp`
- [ ] KCM UI updated for new settings

### Accessibility Compliance
- [ ] Screen reader descriptions are meaningful and contextual
- [ ] Interactive elements can be activated via keyboard
- [ ] Focus indicators are visible
- [ ] High contrast mode tested

---

## 10. Open Questions

1. **Thumbnail performance**: Should thumbnails be opt-in only, or should we detect system capability and auto-disable on low-end hardware?

2. **Multi-monitor OSD**: When switching layouts, show OSD on all monitors simultaneously or just the active one?

3. **Animation duration**: Should movement preview animations match KWin's window animation speed setting, or use independent timing?

4. **Window title truncation**: How many characters before truncating? Should we use app name or window title (or both)?

5. **Stacked window display**: When a zone has multiple windows, show just the count badge, or also list window names (space permitting)?

---

## 11. Success Metrics

| Metric | Target |
|--------|--------|
| OSD provides actionable information | Users can identify window locations without switching |
| Reduced shortcut learning curve | New users discover shortcuts from OSD hints |
| Performance impact | <5ms additional latency on OSD show |
| User satisfaction | Positive feedback on enhanced OSD utility |

---

## 12. Appendix: Current Implementation Reference

### Current LayoutOsd.qml Structure
```
Window (LayerShellQt overlay)
└── Rectangle (container with shadow)
    ├── ZonePreview (zone grid)
    ├── Row
    │   ├── Rectangle (category badge)
    │   └── Label (layout name)
    └── Timer (auto-dismiss)
```

### Current NavigationOsd.qml Structure
```
Window (LayerShellQt overlay)
└── Rectangle (container)
    ├── ZonePreview (optional, on success)
    ├── Label (action + direction arrow)
    └── Timer (auto-dismiss)
```

### Current ZonePreview.qml Structure
```
Item
└── Repeater (zones)
    └── Rectangle (zone)
        ├── Rectangle (border)
        └── Label (zone number)
```
