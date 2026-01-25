# Feature Comparison: PlasmaZones vs KZones

This document compares PlasmaZones with [KZones](https://github.com/gerritdevriese/kzones) to identify feature gaps and potential enhancements.

**Date:** 2026-01-22

---

## Features We Already Have

| Feature | Status | Implementation |
|---------|--------|----------------|
| Zone Selector (top widget) | ✅ | `ZoneSelectorController` - appears when dragging window to top of screen |
| Zone Overlay | ✅ | `OverlayService` - fullscreen display of all zones |
| Edge Snapping | ✅ | Configurable trigger distance via settings |
| Multiple Layouts | ✅ | Full layout management with `LayoutManager` |
| Per-screen layouts | ✅ | `layoutForScreen(screenName, desktop, activity)` |
| Per-virtual-desktop layouts | ✅ | Desktop parameter in layout assignments |
| WYSIWYG Layout Editor | ✅ | Full visual editor with drag-and-drop zone editing |
| Window geometry memory | ✅ | `preSnapGeometry` storage and restoration |
| Window exclusion by class | ✅ | `excludedWindowClasses` setting with pattern matching |
| Configurable polling rate | ✅ | `pollIntervalMs` setting (10-1000ms, default 50ms) |
| System theme colors | ✅ | Kirigami/KDE Plasma integration |
| Multi-zone snap (spanning) | ✅ | `combineZoneGeometries()` for snapping across multiple zones |
| Adjacent zone detection | ✅ | `adjacentZones()` and `getAdjacentZone()` methods |
| Layout cycling shortcuts | ✅ | Previous/Next layout via `ShortcutManager` |
| Quick layout shortcuts (1-9) | ✅ | Direct layout switching with configurable keybindings |
| OSD messages | ✅ | Layout name display on switch |
| Auto-snap new windows | ✅ | "Move new windows to last used zone" feature |
| Keep windows in zones on resolution change | ✅ | `keepWindowsInZonesOnResolutionChange` setting |
| Track windows in zones | ✅ | `getWindowsInZone()` returns windows assigned to a zone |

---

## Features Missing (Potential Enhancements)

### High Priority - Should Implement

#### 1. Keyboard Shortcuts to Move Windows Between Zones
**Source:** KZones feature + Issues #111, #101

**Description:**
- Move active window to adjacent zone (left/right/up/down)
- Move window to specific zone by number
- Cycle through zones with keyboard

**Current State:**
- We have `getAdjacentZone(currentZoneId, direction)` D-Bus method
- We have `adjacentZones()` detection in `Layout` class
- Missing: Global shortcuts and window movement logic

**Implementation Notes:**
- Add shortcuts to `ShortcutManager`: `moveWindowLeft`, `moveWindowRight`, `moveWindowUp`, `moveWindowDown`
- Add `moveWindowToZone(direction)` method that:
  1. Gets currently focused window
  2. Finds its current zone assignment
  3. Uses `getAdjacentZone()` to find target zone
  4. Applies target zone geometry to window

---

#### 2. Push Window to Open/Empty Zone
**Source:** KZones Issue #139 (assigned to v1.0.0 milestone)

**Description:**
Automatically move a window to the first unoccupied zone in the current layout.

**Current State:**
- We track zone occupancy via `getWindowsInZone()`
- We have zone geometry calculation
- Missing: Logic to find empty zones and auto-placement

**Implementation Notes:**
- Add `findEmptyZone()` method to `ZoneDetector` or `LayoutManager`
- Add shortcut "Push to Open Zone"
- Useful for quickly tiling new windows without manual positioning

---

#### 3. Include Mode for App Filtering
**Source:** KZones Issue #158

**Description:**
Currently we only have exclude mode (blacklist). Users want include mode (whitelist) where only specified apps participate in zone snapping.

**Current State:**
- `excludedWindowClasses` setting exists
- `isWindowExcluded()` checks against exclusion list

**Implementation Notes:**
- Add `includedWindowClasses` setting
- Add `filterMode` setting: `exclude` (default) or `include`
- Modify `isWindowExcluded()` to handle both modes:
  ```cpp
  if (filterMode == "include") {
      return !isInIncludeList(windowClass);
  } else {
      return isInExcludeList(windowClass);
  }
  ```

---

#### 4. Zone Selector Shows Layout Cycling/Preview
**Source:** KZones Issue #104

**Description:**
Enhance the zone selector popup to allow previewing and switching between layouts directly, not just selecting zones.

**Current State:**
- `ZoneSelectorController` shows zones from current layout
- Layout switching requires shortcuts or settings

**Implementation Notes:**
- Add layout thumbnails/list to zone selector UI
- Allow hovering to preview layout zones
- Click to switch layout and select zone in one action

---

#### 5. Window Fade/Transparency During Drag
**Source:** KZones feature

**Description:**
Apply transparency effect to window while dragging for better visibility of zones underneath.

**Current State:**
- No visual effect on dragged window
- KWin effect has access to window properties

**Implementation Notes:**
- In `PlasmaZonesEffect`, set window opacity when drag starts
- Restore opacity when drag ends
- Make opacity level configurable (e.g., 70% default)
- Use KWin's `setData()` with `WindowForceBlurRole` or direct opacity manipulation

---

### Medium Priority - Nice to Have

#### 6. Dynamic Zone Resizing Based on Occupancy
**Source:** KZones Issue #163

**Description:**
Zones automatically resize when neighboring zones are empty. For example, if you have a 50/50 split and one zone is empty, the occupied zone could expand to fill the space.

**Current State:**
- Zones have fixed relative geometries
- No awareness of neighboring zone occupancy for resizing

**Implementation Notes:**
- Complex feature requiring:
  - Real-time occupancy monitoring
  - Dynamic geometry recalculation
  - Smooth animation of zone boundaries
  - Rules for when to expand vs. stay fixed
- Consider making this opt-in per-layout

---

#### 7. Resize Multiple Windows at Once (Runtime)
**Source:** KZones Issue #134 (assigned to v1.0.0 milestone)

**Description:**
When a zone boundary is resized at runtime, all windows in affected zones resize accordingly.

**Current State:**
- Editor supports zone resizing with adjacent zone adjustment
- Runtime doesn't support interactive zone boundary adjustment

**Implementation Notes:**
- Would require runtime zone boundary manipulation UI
- When boundary moves, call `setGeometry()` on all windows in affected zones
- Similar to how Windows PowerToys FancyZones handles this

---

#### 8. Switch Between Windows in Same Zone
**Source:** KZones Issue #111

**Description:**
Keyboard shortcut to cycle focus between windows that are snapped to the same zone.

**Current State:**
- `getWindowsInZone(zoneId)` returns list of window IDs in a zone
- No focus cycling logic

**Implementation Notes:**
- Add shortcut "Cycle Windows in Zone"
- Get current focused window's zone
- Get all windows in that zone
- Activate next window in list (wrap around)

---

#### 9. Restore Window to Original Size/Position
**Source:** KZones Issue #159

**Description:**
Keyboard shortcut to restore a window to its pre-snap dimensions and location.

**Current State:**
- We store `preSnapGeometry` for each window
- `getValidatedPreSnapGeometry()` retrieves it
- Used when unsnapping via drag

**Implementation Notes:**
- Add shortcut "Restore Original Size"
- Call existing `getValidatedPreSnapGeometry()`
- Apply geometry to window
- Clear zone assignment

---

### Lower Priority

#### 10. Per-Zone Application Targeting
**Source:** KZones feature

**Description:**
Configure specific applications to automatically snap to specific zones when opened.

**Current State:**
- "Move new windows to last used zone" is a simpler version
- No per-zone app assignment

**Implementation Notes:**
- Add zone property: `targetApplications: ["firefox", "code"]`
- On window open, check if app matches any zone's target list
- If yes, snap to that zone instead of last-used zone

---

#### 11. Nested Zone Selection in Indicator
**Source:** KZones Issue #105

**Description:**
For layouts with overlapping zones, provide UI to select which specific zone to use.

**Current State:**
- `adjacentZones` tracks overlapping zones during detection
- Uses combined geometry for multi-zone snap

**Implementation Notes:**
- When multiple zones overlap at cursor position, show disambiguation UI
- Could be a small popup or keyboard modifier to cycle through options

---

#### 12. OSD Shown Only While Moving Window
**Source:** KZones Issue #181

**Description:**
Option to only show the layout OSD overlay while actively dragging a window, not on layout switch.

**Current State:**
- OSD shows on layout switch
- Overlay shows during drag

**Implementation Notes:**
- Add setting `showOsdOnlyDuringDrag`
- Conditionally skip OSD display in layout switch handler

---

## Implementation Roadmap

### Phase 1: Window Movement Shortcuts
1. Move window to adjacent zone (left/right/up/down)
2. Restore window to original size
3. Push window to open zone

### Phase 2: App Filtering Enhancements
1. Include mode for app filtering
2. Per-zone application targeting (stretch goal)

### Phase 3: UI/UX Polish
1. Window fade during drag
2. Zone selector layout preview/switching
3. OSD display options

### Phase 4: Advanced Features
1. Switch between windows in same zone
2. Dynamic zone resizing (if demand exists)
3. Runtime zone boundary adjustment

---

## References

- KZones Repository: https://github.com/gerritdevriese/kzones
- KZones Enhancement Issues: https://github.com/gerritdevriese/kzones/issues?q=is%3Aissue+is%3Aopen+label%3Aenhancement
- Windows PowerToys FancyZones: https://docs.microsoft.com/en-us/windows/powertoys/fancyzones
