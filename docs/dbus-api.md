# PlasmaZones D-Bus API Reference

Complete D-Bus API documentation for PlasmaZones daemon.

## Service Information

| Property | Value |
|----------|-------|
| **Service Name** | `org.plasmazones` |
| **Object Path** | `/PlasmaZones` |

## Quick Start

```bash
# List all layouts
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getLayoutList

# Get active layout (returns JSON)
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getActiveLayout

# Switch layout by ID
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.setActiveLayout "{uuid}"

# Show/hide overlay
qdbus org.plasmazones /PlasmaZones org.plasmazones.Overlay.showOverlay
qdbus org.plasmazones /PlasmaZones org.plasmazones.Overlay.hideOverlay

# Get all screens
qdbus org.plasmazones /PlasmaZones org.plasmazones.ScreenManager.getScreens
```

---

## Interfaces

- [org.plasmazones.LayoutManager](#orgplasmazoneslayoutmanager) - Layout CRUD and assignment
- [org.plasmazones.Overlay](#orgplasmazonesoverlay) - Zone overlay visibility and highlighting
- [org.plasmazones.ScreenManager](#orgplasmazonesscreenmanager) - Screen information
- [org.plasmazones.WindowTracking](#orgplasmazoneswindowtracking) - Window-zone assignments
- [org.plasmazones.WindowDrag](#orgplasmazoneswindowdrag) - Drag-and-drop handling
- [org.plasmazones.ZoneDetection](#orgplasmazoneszonedetection) - Zone queries
- [org.plasmazones.Settings](#orgplasmazonessettings) - Configuration

---

## org.plasmazones.LayoutManager

Layout management operations including CRUD, screen assignments, and quick layout slots.

### Methods

#### Layout Queries

##### `getActiveLayout() → String`
Get the currently active layout as JSON.

```bash
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getActiveLayout
```

**Returns:** JSON string with full layout data (id, name, zones, etc.)

---

##### `getLayoutList() → StringList`
Get list of all available layouts as JSON array.

```bash
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getLayoutList
```

**Returns:** JSON array of layout objects with id, name, and metadata

---

##### `getLayout(id: String) → String`
Get a specific layout by ID.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | String | Layout UUID |

**Returns:** JSON string with full layout data, or empty if not found

---

#### Layout Management

##### `setActiveLayout(id: String)`
Switch to a different layout.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | String | Layout UUID to activate |

```bash
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.setActiveLayout "550e8400-e29b-41d4-a716-446655440000"
```

---

##### `applyQuickLayout(number: Int, screenName: String)`
Apply a quick layout slot (1-9) to a specific screen.

| Parameter | Type | Description |
|-----------|------|-------------|
| `number` | Int | Slot number (1-9) |
| `screenName` | String | Target screen name |

---

##### `createLayout(name: String, type: String) → String`
Create a new empty layout.

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | String | Display name for the layout |
| `type` | String | Layout type (e.g., "custom") |

**Returns:** UUID of the created layout

---

##### `deleteLayout(id: String)`
Delete a layout permanently.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | String | Layout UUID to delete |

---

##### `duplicateLayout(id: String) → String`
Create a copy of an existing layout.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | String | Layout UUID to duplicate |

**Returns:** UUID of the new layout copy

---

#### Import/Export

##### `importLayout(filePath: String) → String`
Import a layout from a JSON file.

| Parameter | Type | Description |
|-----------|------|-------------|
| `filePath` | String | Absolute path to JSON file |

**Returns:** UUID of the imported layout

---

##### `exportLayout(layoutId: String, filePath: String)`
Export a layout to a JSON file.

| Parameter | Type | Description |
|-----------|------|-------------|
| `layoutId` | String | Layout UUID to export |
| `filePath` | String | Destination file path |

---

#### Editor Support

##### `updateLayout(layoutJson: String) → Bool`
Update an existing layout from JSON (used by editor).

| Parameter | Type | Description |
|-----------|------|-------------|
| `layoutJson` | String | Complete layout JSON with id |

**Returns:** `true` if update succeeded

---

##### `createLayoutFromJson(layoutJson: String) → String`
Create a new layout from JSON data.

| Parameter | Type | Description |
|-----------|------|-------------|
| `layoutJson` | String | Complete layout JSON |

**Returns:** UUID of the created layout

---

##### `openEditor()`
Launch the layout editor for the active layout.

```bash
qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.openEditor
```

---

##### `openEditorForScreen(screenName: String)`
Launch editor for a specific screen's layout.

| Parameter | Type | Description |
|-----------|------|-------------|
| `screenName` | String | Screen to edit layout for |

---

##### `openEditorForLayout(layoutId: String)`
Launch editor for a specific layout.

| Parameter | Type | Description |
|-----------|------|-------------|
| `layoutId` | String | Layout UUID to edit |

---

#### Screen Assignments

##### `getLayoutForScreen(screenName: String) → String`
Get the layout assigned to a screen (all virtual desktops).

| Parameter | Type | Description |
|-----------|------|-------------|
| `screenName` | String | Screen name |

**Returns:** Layout UUID or empty string

---

##### `assignLayoutToScreen(screenName: String, layoutId: String)`
Assign a layout to a screen for all virtual desktops.

| Parameter | Type | Description |
|-----------|------|-------------|
| `screenName` | String | Target screen |
| `layoutId` | String | Layout UUID |

---

##### `clearAssignment(screenName: String)`
Remove layout assignment from a screen.

| Parameter | Type | Description |
|-----------|------|-------------|
| `screenName` | String | Screen to clear |

---

#### Per-Virtual-Desktop Assignments

##### `getLayoutForScreenDesktop(screenName: String, virtualDesktop: Int) → String`
Get layout for a specific screen and virtual desktop combination.

| Parameter | Type | Description |
|-----------|------|-------------|
| `screenName` | String | Screen name |
| `virtualDesktop` | Int | Virtual desktop number (1-based, 0 = all) |

**Returns:** Layout UUID or empty string

---

##### `assignLayoutToScreenDesktop(screenName: String, virtualDesktop: Int, layoutId: String)`
Assign layout to a specific screen/desktop combination.

| Parameter | Type | Description |
|-----------|------|-------------|
| `screenName` | String | Target screen |
| `virtualDesktop` | Int | Virtual desktop (1-based, 0 = all) |
| `layoutId` | String | Layout UUID |

---

##### `clearAssignmentForScreenDesktop(screenName: String, virtualDesktop: Int)`
Clear assignment for a screen/desktop combination.

---

##### `hasExplicitAssignmentForScreenDesktop(screenName: String, virtualDesktop: Int) → Bool`
Check if there's an explicit assignment (not inherited).

**Returns:** `true` if explicit assignment exists

---

#### Virtual Desktop Information

##### `getVirtualDesktopCount() → Int`
Get number of virtual desktops.

**Returns:** Desktop count

---

##### `getVirtualDesktopNames() → StringList`
Get names of all virtual desktops.

**Returns:** List of desktop names

---

##### `getAllScreenAssignments() → String`
Get all screen-to-layout assignments as JSON.

**Returns:** JSON object mapping screens to layouts

---

#### Quick Layout Slots (1-9)

##### `getQuickLayoutSlot(slotNumber: Int) → String`
Get layout assigned to a quick slot.

| Parameter | Type | Description |
|-----------|------|-------------|
| `slotNumber` | Int | Slot number (1-9) |

**Returns:** Layout UUID or empty string

---

##### `setQuickLayoutSlot(slotNumber: Int, layoutId: String)`
Assign a layout to a quick slot.

| Parameter | Type | Description |
|-----------|------|-------------|
| `slotNumber` | Int | Slot number (1-9) |
| `layoutId` | String | Layout UUID (empty to clear) |

---

##### `getAllQuickLayoutSlots() → VariantMap`
Get all quick slot assignments.

**Returns:** Map of slot numbers to layout UUIDs

---

### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `daemonReady` | - | Daemon fully initialized |
| `layoutChanged` | `layoutJson: String` | Active layout modified |
| `layoutListChanged` | - | Layout list modified |
| `screenLayoutChanged` | `screenName: String, layoutId: String` | Screen assignment changed |
| `virtualDesktopCountChanged` | `count: Int` | Desktop count changed |
| `activeLayoutIdChanged` | `layoutId: String` | Active layout switched |
| `quickLayoutSlotsChanged` | - | Quick slots modified |

---

## org.plasmazones.Overlay

Zone overlay visibility and highlighting control.

### Methods

#### Visibility Control

##### `showOverlay()`
Show the zone overlay on all screens.

```bash
qdbus org.plasmazones /PlasmaZones org.plasmazones.Overlay.showOverlay
```

---

##### `hideOverlay()`
Hide the zone overlay.

```bash
qdbus org.plasmazones /PlasmaZones org.plasmazones.Overlay.hideOverlay
```

---

##### `isOverlayVisible() → Bool`
Check if overlay is currently visible.

**Returns:** `true` if overlay is shown

---

#### Zone Highlighting

##### `highlightZone(zoneId: String)`
Highlight a single zone.

| Parameter | Type | Description |
|-----------|------|-------------|
| `zoneId` | String | Zone UUID to highlight |

---

##### `highlightZones(zoneIds: StringList)`
Highlight multiple zones simultaneously.

| Parameter | Type | Description |
|-----------|------|-------------|
| `zoneIds` | StringList | List of zone UUIDs |

---

##### `clearHighlight()`
Clear all zone highlights.

---

#### Configuration

##### `getPollIntervalMs() → Int`
Get the overlay poll interval in milliseconds.

**Returns:** Poll interval (ms)

---

##### `getMinimumZoneSizePx() → Int`
Get minimum zone size for detection (pixels).

**Returns:** Minimum size (px)

---

##### `getMinimumZoneDisplaySizePx() → Int`
Get minimum zone size for display (pixels).

**Returns:** Minimum display size (px)

---

##### `switchToLayout(layoutId: String)`
Switch to a specific layout (convenience method).

| Parameter | Type | Description |
|-----------|------|-------------|
| `layoutId` | String | Layout UUID |

---

### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `overlayVisibilityChanged` | `visible: Bool` | Visibility state changed |
| `zoneHighlightChanged` | `zoneId: String` | Highlight changed |
| `layoutSwitched` | `layoutId: String` | Layout was switched |

---

## org.plasmazones.ScreenManager

Screen information and monitoring.

### Methods

##### `getScreens() → StringList`
Get list of all screen names.

```bash
qdbus org.plasmazones /PlasmaZones org.plasmazones.ScreenManager.getScreens
```

**Returns:** List of screen names (e.g., `["DP-1", "HDMI-1"]`)

---

##### `getScreenInfo(screenName: String) → String`
Get detailed information about a screen.

| Parameter | Type | Description |
|-----------|------|-------------|
| `screenName` | String | Screen to query |

**Returns:** JSON with geometry, scale, refresh rate, etc.

---

##### `getPrimaryScreen() → String`
Get the primary screen name.

**Returns:** Primary screen name

---

##### `getScreenCount() → Int`
Get total number of screens.

**Returns:** Screen count

---

### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `screenAdded` | `screenName: String` | New screen connected |
| `screenRemoved` | `screenName: String` | Screen disconnected |
| `screenGeometryChanged` | `screenName: String` | Screen resolution/position changed |

---

## org.plasmazones.WindowTracking

Window-zone assignment tracking and keyboard navigation.

### Methods

#### Window Snap Notifications

##### `windowSnapped(windowId: String, zoneId: String)`
Record that a window was snapped to a zone.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |
| `zoneId` | String | Zone UUID |

---

##### `windowUnsnapped(windowId: String)`
Record that a window was unsnapped.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |

---

##### `setWindowSticky(windowId: String, sticky: Bool)`
Set whether a window is on all virtual desktops.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |
| `sticky` | Bool | `true` if on all desktops |

---

##### `windowUnsnappedForFloat(windowId: String)`
Unsnap for float toggle (saves zone for later restore).

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |

---

##### `windowClosed(windowId: String)`
Clean up all tracking data for a closed window.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Closed window identifier |

---

#### Pre-Snap Geometry

##### `storePreSnapGeometry(windowId: String, x: Int, y: Int, width: Int, height: Int)`
Store window geometry before snapping (for restore).

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |
| `x` | Int | X position |
| `y` | Int | Y position |
| `width` | Int | Window width |
| `height` | Int | Window height |

**Note:** Only stores on first snap; subsequent snaps keep original.

---

##### `getPreSnapGeometry(windowId: String, x: out Int, y: out Int, width: out Int, height: out Int) → Bool`
Retrieve stored pre-snap geometry.

**Returns:** `true` if geometry was found

---

##### `hasPreSnapGeometry(windowId: String) → Bool`
Check if pre-snap geometry exists.

**Returns:** `true` if stored

---

##### `clearPreSnapGeometry(windowId: String)`
Clear stored pre-snap geometry.

---

##### `getValidatedPreSnapGeometry(windowId: String, x: out Int, y: out Int, width: out Int, height: out Int) → Bool`
Get pre-snap geometry validated against current screen bounds.

**Returns:** `true` if found and validated

---

##### `isGeometryOnScreen(x: Int, y: Int, width: Int, height: Int) → Bool`
Check if geometry is visible on any screen.

**Returns:** `true` if at least partially visible

---

#### Float State

##### `getPreFloatZone(windowId: String, zoneIdOut: out String) → Bool`
Get the zone to restore to when unfloating.

**Returns:** `true` if window had a zone before floating

---

##### `clearPreFloatZone(windowId: String)`
Clear saved pre-float zone.

---

#### Window Queries

##### `getZoneForWindow(windowId: String) → String`
Get the zone a window is snapped to.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |

**Returns:** Zone UUID or empty string

---

##### `getMultiZoneForWindow(windowId: String) → StringList`
Get all zones a window spans (multi-zone snap).

**Returns:** List of zone UUIDs

---

##### `getWindowsInZone(zoneId: String) → StringList`
Get all windows snapped to a zone.

| Parameter | Type | Description |
|-----------|------|-------------|
| `zoneId` | String | Zone UUID |

**Returns:** List of window identifiers

---

##### `getSnappedWindows() → StringList`
Get all currently snapped windows.

**Returns:** List of window identifiers

---

##### `getLastUsedZoneId() → String`
Get the last zone a window was snapped to.

**Returns:** Zone UUID or empty string

---

#### Auto-Snap Features

##### `snapToLastZone(windowId: String, windowScreenName: String, sticky: Bool, snapX: out Int, snapY: out Int, snapWidth: out Int, snapHeight: out Int, shouldSnap: out Bool)`
Calculate snap geometry for auto-snap to last used zone.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window to snap |
| `windowScreenName` | String | Window's current screen |
| `sticky` | Bool | Is window sticky |

**Note:** Checks `moveNewWindowsToLastZone` setting internally.

---

##### `recordSnapIntent(windowId: String, wasUserInitiated: Bool)`
Record whether a snap was user-initiated (for auto-snap logic).

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |
| `wasUserInitiated` | Bool | `true` if user dragged |

---

##### `restoreToPersistedZone(windowId: String, screenName: String, sticky: Bool, snapX: out Int, snapY: out Int, snapWidth: out Int, snapHeight: out Int, shouldRestore: out Bool)`
Restore window to its zone from previous session.

---

##### `getUpdatedWindowGeometries() → String`
Get updated geometries for all tracked windows (resolution change handling).

**Returns:** JSON array: `[{windowId, x, y, width, height}, ...]`

---

#### Keyboard Navigation

##### `moveWindowToAdjacentZone(direction: String)`
Move focused window to adjacent zone.

| Parameter | Type | Description |
|-----------|------|-------------|
| `direction` | String | `"left"`, `"right"`, `"up"`, `"down"` |

---

##### `focusAdjacentZone(direction: String)`
Focus a window in an adjacent zone.

| Parameter | Type | Description |
|-----------|------|-------------|
| `direction` | String | `"left"`, `"right"`, `"up"`, `"down"` |

---

##### `pushToEmptyZone()`
Push focused window to first empty zone.

---

##### `restoreWindowSize()`
Restore focused window to pre-snap size.

---

##### `toggleWindowFloat()`
Toggle float state for focused window.

---

##### `isWindowFloating(windowId: String) → Bool`
Check if window is floating.

**Returns:** `true` if floating

---

##### `queryWindowFloating(windowId: String) → Bool`
Query float state (D-Bus callable for effect sync).

**Returns:** `true` if floating

---

##### `setWindowFloating(windowId: String, floating: Bool)`
Set window's float state.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |
| `floating` | Bool | `true` to float |

---

##### `getFloatingWindows() → StringList`
Get all floating window IDs.

**Returns:** List of floating window identifiers

---

##### `findEmptyZone() → String`
Find first empty zone in current layout.

**Returns:** Zone UUID or empty string

---

##### `getZoneGeometry(zoneId: String) → String`
Get zone geometry (uses primary screen).

| Parameter | Type | Description |
|-----------|------|-------------|
| `zoneId` | String | Zone UUID |

**Returns:** JSON: `{x, y, width, height}` or empty

---

##### `getZoneGeometryForScreen(zoneId: String, screenName: String) → String`
Get zone geometry for specific screen.

| Parameter | Type | Description |
|-----------|------|-------------|
| `zoneId` | String | Zone UUID |
| `screenName` | String | Screen name (empty = primary) |

**Returns:** JSON: `{x, y, width, height}` or empty

---

#### State Persistence

##### `saveState()`
Save window tracking state to disk.

---

##### `loadState()`
Load window tracking state from disk.

---

### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `windowZoneChanged` | `windowId: String, zoneId: String` | Window zone assignment changed |
| `navigationFeedback` | `success: Bool, action: String, reason: String` | Navigation result feedback |
| `moveWindowToZoneRequested` | `targetZoneId: String, zoneGeometry: String` | Request to move window |
| `focusWindowInZoneRequested` | `targetZoneId: String, windowId: String` | Request to focus window |
| `restoreWindowRequested` | - | Request to restore window size |
| `toggleWindowFloatRequested` | `shouldFloat: Bool` | Request to toggle float |

---

## org.plasmazones.WindowDrag

Window drag-and-drop handling for zone snapping.

### Methods

##### `dragStarted(windowId: String, x: Double, y: Double, width: Double, height: Double, appName: String, windowClass: String)`
Called when window drag begins.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |
| `x` | Double | Window X position |
| `y` | Double | Window Y position |
| `width` | Double | Window width |
| `height` | Double | Window height |
| `appName` | String | Application name (for exclusion) |
| `windowClass` | String | Window class (for exclusion) |

**Note:** Parameters are Double because KWin QML sends JS numbers as D-Bus doubles.

---

##### `dragMoved(windowId: String, cursorX: Int, cursorY: Int, modifiers: Int)`
Called while window is being dragged.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |
| `cursorX` | Int | Cursor X position |
| `cursorY` | Int | Cursor Y position |
| `modifiers` | Int | Qt::KeyboardModifiers flags |

**Modifier Values:**
- `0x02000000` = Shift
- `0x04000000` = Control
- `0x08000000` = Alt
- `0x10000000` = Meta

---

##### `dragStopped(windowId: String, snapX: out Int, snapY: out Int, snapWidth: out Int, snapHeight: out Int, shouldApplyGeometry: out Bool)`
Called when window drag ends.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Window identifier |
| `snapX` | out Int | Snap X position |
| `snapY` | out Int | Snap Y position |
| `snapWidth` | out Int | Snap width |
| `snapHeight` | out Int | Snap height |
| `shouldApplyGeometry` | out Bool | Whether to apply geometry |

---

##### `cancelSnap()`
Cancel current snap operation (Escape key pressed).

---

##### `handleWindowClosed(windowId: String)`
Clean up drag state for closed window.

| Parameter | Type | Description |
|-----------|------|-------------|
| `windowId` | String | Closed window identifier |

---

## org.plasmazones.ZoneDetection

Zone detection and navigation queries.

### Methods

#### Zone Detection

##### `detectZoneAtPosition(x: Int, y: Int) → String`
Find zone at cursor position.

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | Int | Screen X coordinate |
| `y` | Int | Screen Y coordinate |

**Returns:** Zone UUID or empty string

---

##### `detectMultiZoneAtPosition(x: Int, y: Int) → StringList`
Find adjacent zones at position (for multi-zone snap).

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | Int | Screen X coordinate |
| `y` | Int | Screen Y coordinate |

**Returns:** List of adjacent zone UUIDs

---

##### `detectZoneWithModifiers(x: Int, y: Int) → String`
Combined zone detection with modifier state.

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | Int | Screen X coordinate |
| `y` | Int | Screen Y coordinate |

**Returns:** `"zoneId;modifiers"` (e.g., `"uuid-here;33554432"` for Shift)

---

#### Zone Geometry

##### `getZoneGeometry(zoneId: String) → String`
Get zone geometry (primary screen).

| Parameter | Type | Description |
|-----------|------|-------------|
| `zoneId` | String | Zone UUID |

**Returns:** JSON: `{x, y, width, height}`

---

##### `getZoneGeometryForScreen(zoneId: String, screenName: String) → String`
Get zone geometry for specific screen.

| Parameter | Type | Description |
|-----------|------|-------------|
| `zoneId` | String | Zone UUID |
| `screenName` | String | Target screen |

**Returns:** JSON: `{x, y, width, height}`

---

##### `getZonesForScreen(screenName: String) → StringList`
Get all zones on a screen.

| Parameter | Type | Description |
|-----------|------|-------------|
| `screenName` | String | Screen name |

**Returns:** List of zone UUIDs

---

##### `getAllZoneGeometries() → StringList`
Get geometries for all zones in active layout.

**Returns:** List of JSON geometry objects

---

#### Zone Navigation

##### `getAdjacentZone(currentZoneId: String, direction: String) → String`
Get neighboring zone in a direction.

| Parameter | Type | Description |
|-----------|------|-------------|
| `currentZoneId` | String | Starting zone UUID |
| `direction` | String | `"left"`, `"right"`, `"up"`, `"down"` |

**Returns:** Adjacent zone UUID or empty string

---

##### `getFirstZoneInDirection(direction: String) → String`
Get edge zone for initial navigation (when window not snapped).

| Parameter | Type | Description |
|-----------|------|-------------|
| `direction` | String | `"left"`, `"right"`, `"up"`, `"down"` |

**Returns:**
- `left`: Leftmost zone (smallest x)
- `right`: Rightmost zone (largest x + width)
- `up`: Topmost zone (smallest y)
- `down`: Bottommost zone (largest y + height)

---

##### `getZoneByNumber(zoneNumber: Int) → String`
Get zone by its number (1-indexed).

| Parameter | Type | Description |
|-----------|------|-------------|
| `zoneNumber` | Int | Zone number (1-9) |

**Returns:** Zone UUID or empty string

---

#### Keyboard State

##### `getKeyboardModifiers() → Int`
Get current keyboard modifier state.

**Returns:** Qt::KeyboardModifiers bitmask

| Value | Modifier |
|-------|----------|
| `0x02000000` | Shift |
| `0x04000000` | Control |
| `0x08000000` | Alt |
| `0x10000000` | Meta |

---

### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `zoneDetected` | `zoneId: String, geometry: String` | Zone detected at position |

---

## org.plasmazones.Settings

Configuration management and shader queries.

### Methods

#### Settings Management

##### `reloadSettings()`
Reload settings from disk.

```bash
qdbus org.plasmazones /PlasmaZones org.plasmazones.Settings.reloadSettings
```

---

##### `saveSettings()`
Save current settings to disk.

---

##### `resetToDefaults()`
Reset all settings to defaults.

---

##### `getAllSettings() → String`
Get all settings as JSON.

**Returns:** JSON object with all setting key-value pairs

---

##### `getSetting(key: String) → Variant`
Get a single setting value.

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | String | Setting key name |

**Returns:** Setting value (type varies)

---

##### `setSetting(key: String, value: Variant) → Bool`
Set a single setting value.

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | String | Setting key name |
| `value` | Variant | New value |

**Returns:** `true` if setting was updated

---

##### `getSettingKeys() → StringList`
Get list of all setting key names.

**Returns:** List of setting keys

---

#### Shader Queries

##### `availableShaders() → VariantList`
Get list of available shader effects.

**Returns:** List of shader metadata objects (id, name, description, etc.)

---

##### `shaderInfo(shaderId: String) → VariantMap`
Get detailed information about a shader.

| Parameter | Type | Description |
|-----------|------|-------------|
| `shaderId` | String | Shader UUID |

**Returns:** Shader metadata map or empty if not found

---

##### `defaultShaderParams(shaderId: String) → VariantMap`
Get default parameter values for a shader.

| Parameter | Type | Description |
|-----------|------|-------------|
| `shaderId` | String | Shader UUID |

**Returns:** Map of parameter IDs to default values

---

##### `shadersEnabled() → Bool`
Check if shader effects are available.

**Returns:** `true` if compiled with shader support

---

##### `userShadersEnabled() → Bool`
Check if user-installed shaders are supported.

**Returns:** `true` if user shaders can be loaded

---

##### `userShaderDirectory() → String`
Get user shader installation directory.

**Returns:** Path (typically `~/.local/share/plasmazones/shaders`)

---

##### `openUserShaderDirectory()`
Open user shader directory in file manager.

---

##### `refreshShaders()`
Reload all shaders from disk.

---

### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `settingsChanged` | - | Settings were modified |

---

## Common Patterns

### Listening for Layout Changes

```bash
# Monitor layout changes
dbus-monitor "interface='org.plasmazones.LayoutManager',member='activeLayoutIdChanged'"
```

### Scripting Example

```bash
#!/bin/bash
# Cycle through layouts

LAYOUTS=$(qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getLayoutList | jq -r '.[].id')
CURRENT=$(qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getActiveLayout | jq -r '.id')

# Find next layout
NEXT=""
FOUND=false
for id in $LAYOUTS; do
    if $FOUND; then
        NEXT=$id
        break
    fi
    if [ "$id" = "$CURRENT" ]; then
        FOUND=true
    fi
done

# Wrap around if at end
if [ -z "$NEXT" ]; then
    NEXT=$(echo "$LAYOUTS" | head -1)
fi

qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.setActiveLayout "$NEXT"
```

### Python Example

```python
import dbus

bus = dbus.SessionBus()
layout_mgr = bus.get_object('org.plasmazones', '/PlasmaZones')
iface = dbus.Interface(layout_mgr, 'org.plasmazones.LayoutManager')

# Get all layouts
layouts = iface.getLayoutList()
print(f"Available layouts: {layouts}")

# Switch layout
iface.setActiveLayout("your-layout-uuid-here")
```

---

## Error Handling

Most methods return empty strings or `false` on failure rather than throwing exceptions. Check return values:

```bash
RESULT=$(qdbus org.plasmazones /PlasmaZones org.plasmazones.LayoutManager.getLayout "invalid-uuid")
if [ -z "$RESULT" ]; then
    echo "Layout not found"
fi
```

---

## Version History

| Version | Changes |
|---------|---------|
| 1.0.0 | Initial release with LayoutManager and Overlay |
| 1.1.0 | Added WindowTracking, WindowDrag, ZoneDetection |
| 1.2.0 | Added Settings, keyboard navigation, session persistence |
| 1.3.0 | Added per-virtual-desktop assignments, quick layout slots |
