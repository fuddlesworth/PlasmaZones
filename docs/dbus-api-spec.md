# PlasmaZones D-Bus API Specification

**Version**: 1.1
**Service**: `org.plasmazones`
**Object Path**: `/PlasmaZones`
**Interfaces**: 7 (+ 3 planned)

## Stability Tiers

| Tier | Guarantee | Scope |
|------|-----------|-------|
| **Stable** | No breaking changes within major version | All v1.0 methods/signals |
| **v1.1** | Additive only, may refine before v2 | New methods added in this document |
| **Experimental** | May change before stabilization | CompositorBridge protocol |
| **Internal** | No external stability guarantee | Methods annotated `@internal` |

## Window ID Format

All window identifiers use the format `appId|bridgeHandle`:

- **appId**: Application identity (`desktopFileName()` preferred, normalized `windowClass()` fallback), lowercased
- **bridgeHandle**: Compositor-specific opaque token (KWin: `QUuid::toString(QUuid::WithoutBraces)`)

The daemon splits on `|` to extract `appId` for persistence, app rules, and multi-instance tracking. The `bridgeHandle` is passed back to the compositor verbatim and never parsed by the daemon.

---

## 1. org.plasmazones.WindowTracking

Window-to-zone assignment tracking, floating state, navigation commands, and snap restoration.

### Methods

| Method | Signature | Returns | Stability | Purpose |
|--------|-----------|---------|-----------|---------|
| `windowSnapped` | `(s windowId, s zoneId, s screenId)` | void | Stable | Record window snapped to zone |
| `windowSnappedMultiZone` | `(s windowId, as zoneIds, s screenId)` | void | Stable | Record multi-zone snap |
| `windowUnsnapped` | `(s windowId)` | void | Stable | Record window unsnapped |
| `windowsSnappedBatch` | `(s batchJson)` | void | Stable | Batch snap confirmations (JSON) |
| `windowScreenChanged` | `(s windowId, s newScreenId)` | void | Stable | Notify cross-screen move |
| `setWindowSticky` | `(s windowId, b sticky)` | void | Stable | Set all-desktops state |
| `windowClosed` | `(s windowId)` | void | Stable | Clean up tracking for closed window |
| `windowActivated` | `(s windowId, s screenId)` | void | Stable | Notify window gained focus |
| `cursorScreenChanged` | `(s screenId)` | void | Stable | Cursor crossed to different monitor |
| `getZoneForWindow` | `(s windowId)` | `s zoneId` | Stable | Query current zone |
| `getMultiZoneForWindow` | `(s windowId)` | `as zoneIds` | Stable | Query all zones window spans |
| `getWindowsInZone` | `(s zoneId)` | `as windowIds` | Stable | Get windows in zone |
| `getSnappedWindows` | `()` | `as windowIds` | Stable | Get all snapped windows |
| `getEmptyZonesJson` | `(s screenId)` | `s json` | Stable | Empty zones with geometry (JSON) |
| `getLastUsedZoneId` | `()` | `s zoneId` | Stable | Last used zone for auto-snap |
| `storePreTileGeometry` | `(s windowId, i x, i y, i w, i h, s screenId, b overwrite)` | void | Stable | Store geometry before snap/tile |
| `hasPreTileGeometry` | `(s windowId)` | `b` | Stable | Check if pre-tile geometry exists |
| `getPreTileGeometry` | `(s windowId)` | `b found, i x, i y, i w, i h` | Stable | Get raw pre-tile geometry |
| `getValidatedPreTileGeometry` | `(s windowId)` | `b found, i x, i y, i w, i h` | Stable | Get pre-tile geometry (screen-validated) |
| `clearPreTileGeometry` | `(s windowId)` | void | Stable | Clear stored pre-tile geometry |
| `getPreTileGeometriesJson` | `()` | `s json` | Stable | All pre-tile geometries as JSON |
| `setWindowFloating` | `(s windowId, b floating)` | void | Stable | Set floating state |
| `isWindowFloating` | `(s windowId)` | `b` | Stable | Check floating state |
| `queryWindowFloating` | `(s windowId)` | `b` | Stable | Query floating (for effect sync) |
| `getFloatingWindows` | `()` | `as windowIds` | Stable | Get all floating windows |
| `windowUnsnappedForFloat` | `(s windowId)` | void | Stable | Unsnap for float (saves pre-float zone) |
| `getPreFloatZone` | `(s windowId)` | `b found, s zoneId` | Stable | Get zone to restore on unfloat |
| `clearPreFloatZone` | `(s windowId)` | void | Stable | Clear pre-float zone |
| `calculateUnfloatRestore` | `(s windowId, s screenId)` | `s json` | Stable | Calculate unfloat geometry+zones |
| `getZoneGeometry` | `(s zoneId)` | `s json` | Stable | Zone geometry (primary screen) |
| `getZoneGeometryForScreen` | `(s zoneId, s screenId)` | `s json` | Stable | Zone geometry for screen |
| `findEmptyZone` | `()` | `s zoneId` | Stable | First empty zone ID |
| `snapToAppRule` | `(s windowId, s screenId, b sticky)` | `i x, i y, i w, i h, b shouldSnap` | Stable | Auto-snap via app rule |
| `snapToLastZone` | `(s windowId, s screenId, b sticky)` | `i x, i y, i w, i h, b shouldSnap` | Stable | Auto-snap to last used zone |
| `snapToEmptyZone` | `(s windowId, s screenId, b sticky)` | `i x, i y, i w, i h, b shouldSnap` | Stable | Auto-assign to first empty zone |
| `restoreToPersistedZone` | `(s windowId, s screenId, b sticky)` | `i x, i y, i w, i h, b shouldRestore` | Stable | Restore from session persistence |
| `resolveWindowRestore` | `(s windowId, s screenId, b sticky)` | `i x, i y, i w, i h, b shouldSnap` | Stable | Full 4-level snap fallback chain |
| `getUpdatedWindowGeometries` | `()` | `s json` | Stable | Updated geometries after resolution change |
| `recordSnapIntent` | `(s windowId, b wasUserInitiated)` | void | Stable | Record user vs auto snap |
| `reportNavigationFeedback` | `(b success, s action, s reason, s sourceZoneId, s targetZoneId, s screenId)` | void | Stable | Report nav result for OSD |
| `toggleFloatForWindow` | `(s windowId, s screenId)` | void | Stable | Daemon-driven float toggle |
| `setWindowFloatingForScreen` | `(s windowId, s screenId, b floating)` | void | Stable | Explicit directional float set |
| `applyGeometryForFloat` | `(s windowId, s screenId)` | `b applied` | Stable | Apply pre-tile geometry on float |
| `requestMoveSpecificWindowToZone` | `(s windowId, s zoneId, s geometryJson)` | void | Stable | Snap Assist: move specific window |
| `saveState` | `()` | void | Stable | Persist tracking state |
| `loadState` | `()` | void | Stable | Load persisted state |
| `saveStateOnShutdown` | `()` | void | Internal | Flush state on daemon exit |
| `requestReapplyWindowGeometries` | `()` | void | Internal | Trigger geometry reapply |
| `handleBatchedResnap` | `(s resnapData)` | void | Internal | Process batched resnap from SnapEngine |
| **Navigation (daemon-driven)** | | | |
| `moveWindowToAdjacentZone` | `(s direction)` | void | Stable | Move active window to adjacent zone |
| `focusAdjacentZone` | `(s direction)` | void | Stable | Focus window in adjacent zone |
| `pushToEmptyZone` | `(s screenId)` | void | Stable | Push active window to first empty zone |
| `restoreWindowSize` | `()` | void | Stable | Restore active window to pre-snap size |
| `toggleWindowFloat` | `()` | void | Stable | Toggle float for active window |
| `swapWindowWithAdjacentZone` | `(s direction)` | void | Stable | Swap active window with adjacent |
| `snapToZoneByNumber` | `(i zoneNumber, s screenId)` | void | Stable | Snap active window to zone 1-9 |
| `rotateWindowsInLayout` | `(b clockwise, s screenId)` | void | Stable | Rotate all windows in layout |
| `cycleWindowsInZone` | `(b forward)` | void | Stable | Cycle focus within zone |
| `resnapToNewLayout` | `()` | void | Stable | Resnap windows after layout change |
| `resnapCurrentAssignments` | `(s screenFilter)` | void | Stable | Resnap from current assignments |
| `resnapFromAutotileOrder` | `(as windowOrder, s screenId)` | void | Stable | Resnap using autotile window order |
| `snapAllWindows` | `(s screenId)` | void | Stable | Trigger snap-all on screen |
| `calculateSnapAllWindows` | `(as windowIds, s screenId)` | `s json` | Stable | Calculate snap-all assignments |
| **Target computation** | | | |
| `getMoveTargetForWindow` | `(s windowId, s direction, s screenId)` | `s json` | Stable | Compute move target geometry |
| `getFocusTargetForWindow` | `(s windowId, s direction, s screenId)` | `s json` | Stable | Compute focus target window |
| `getPushTargetForWindow` | `(s windowId, s screenId)` | `s json` | Stable | Compute push target geometry |
| `getSwapTargetForWindow` | `(s windowId, s direction, s screenId)` | `s json` | Stable | Compute swap target data |
| `getSnapToZoneByNumberTarget` | `(s windowId, i zoneNumber, s screenId)` | `s json` | Stable | Compute snap-by-number geometry |
| `getRestoreForWindow` | `(s windowId, s screenId)` | `s json` | Stable | Compute restore geometry |
| `getCycleTargetForWindow` | `(s windowId, b forward, s screenId)` | `s json` | Stable | Compute cycle target window |
| **Planned v1.1** | | | |
| `moveWindowToZone` | `(s windowId, s zoneId)` | void | v1.1 | Convenience: snap window to zone |
| `swapWindowsById` | `(s windowId1, s windowId2)` | void | v1.1 | Convenience: swap two windows |
| `getWindowState` | `(s windowId)` | `s json` | v1.1 | Window state snapshot |
| `getAllWindowStates` | `()` | `s json` | v1.1 | All window states for TUI |

### Signals

| Signal | Signature | Stability | Purpose |
|--------|-----------|-----------|---------|
| `windowZoneChanged` | `(s windowId, s zoneId)` | Stable | Zone assignment changed |
| `windowFloatingChanged` | `(s windowId, b isFloating, s screenId)` | Stable | Floating state changed |
| `toggleWindowFloatRequested` | `(b shouldFloat)` | Stable | Float toggle shortcut pressed |
| `navigationFeedback` | `(b success, s action, s reason, s sourceZoneId, s targetZoneId, s screenId)` | Stable | Navigation result for OSD |
| `pendingRestoresAvailable` | `()` | Stable | Pending zone restores ready |
| `reapplyWindowGeometriesRequested` | `()` | Stable | Reapply after panel change |
| `snapAllWindowsRequested` | `(s screenId)` | Stable | Effect should collect candidates |
| `moveSpecificWindowToZoneRequested` | `(s windowId, s zoneId, s geometryJson)` | Stable | Move specific window (Snap Assist) |
| `applyGeometryRequested` | `(s windowId, s geometryJson, s zoneId, s screenId)` | Stable | Apply geometry to window |
| `activateWindowRequested` | `(s windowId)` | Stable | Activate/focus window |
| `applyGeometriesBatch` | `(s batchJson, s action)` | Stable | Batch geometry application |
| `raiseWindowsRequested` | `(as windowIds)` | Stable | Raise windows in order |
| **Planned v1.1** | | | |
| `windowStateChanged` | `(s windowId, s stateJson)` | v1.1 | Unified change stream |

---

## 2. org.plasmazones.Autotile

Autotile engine control: algorithms, window operations, focus, master/stack management.

### Properties

| Property | Type | Access | Purpose |
|----------|------|--------|---------|
| `enabled` | `b` | Read | Autotiling active on any screen |
| `autotileScreens` | `as` | Read | Screens using autotile |
| `algorithm` | `s` | Read/Write | Current tiling algorithm ID |
| `masterRatio` | `d` | Read/Write | Master area ratio (0.1-0.9) |
| `masterCount` | `i` | Read/Write | Windows in master area (1-5) |
| `innerGap` | `i` | Read/Write | Gap between windows (0-50px) |
| `outerGap` | `i` | Read/Write | Gap from edges (0-50px) |
| `smartGaps` | `b` | Read/Write | Hide gaps with single window |
| `focusNewWindows` | `b` | Read/Write | Auto-focus new windows |

### Methods

| Method | Signature | Returns | Purpose |
|--------|-----------|---------|---------|
| `retile` | `(s screenId)` | void | Force retile (empty=all) |
| `retileAllScreens` | `()` | void | Retile all autotile screens |
| `swapWindows` | `(s windowId1, s windowId2)` | void | Swap positions |
| `promoteToMaster` | `(s windowId)` | void | Promote to master area |
| `demoteFromMaster` | `(s windowId)` | void | Demote to stack |
| `focusMaster` | `()` | void | Focus master window |
| `focusNext` | `()` | void | Focus next tiled window |
| `focusPrevious` | `()` | void | Focus previous tiled window |
| `notifyWindowFocused` | `(s windowId, s screenId)` | void | Window focus notification |
| `increaseMasterRatio` | `(d delta)` | void | Increase master ratio |
| `decreaseMasterRatio` | `(d delta)` | void | Decrease master ratio |
| `increaseMasterCount` | `()` | void | Add window to master |
| `decreaseMasterCount` | `()` | void | Remove window from master |
| `availableAlgorithms` | `()` | `as` | List algorithm IDs |
| `algorithmInfo` | `(s algorithmId)` | `s json` | Algorithm metadata |
| `windowOpened` | `(s windowId, s screenId, i minW, i minH)` | void | Window opened notification |
| `windowsOpenedBatch` | `(s batchJson)` | void | Batch open notifications |
| `windowMinSizeUpdated` | `(s windowId, i minW, i minH)` | void | Min size changed |
| `windowClosed` | `(s windowId)` | void | Window closed |
| `toggleWindowFloat` | `(s windowId, s screenId)` | void | Toggle float in autotile |
| `setWindowFloat` | `(s windowId, s screenId, b floating)` | void | Set float in autotile |

### Signals

| Signal | Signature | Purpose |
|--------|-----------|---------|
| `enabledChanged` | `(b enabled)` | Autotile enabled/disabled |
| `autotileScreensChanged` | `(as screenIds, b isDesktopSwitch)` | Autotile screen set changed |
| `algorithmChanged` | `(s algorithmId)` | Algorithm changed |
| `tilingChanged` | `(s screenId)` | Tiling layout changed |
| `windowsTileRequested` | `(s tileRequestsJson)` | Apply tiled geometries (batch) |
| `focusWindowRequested` | `(s windowId)` | Focus window |
| `configChanged` | `()` | Any config property changed |
| `windowsReleasedFromTiling` | `(as windowIds)` | Windows removed from autotile |
| `windowFloatingChanged` | `(s windowId, b isFloating, s screenId)` | Float state changed |

---

## 3. org.plasmazones.Overlay

Zone overlay visibility, highlighting, Snap Assist, and shader preview.

### Methods

| Method | Signature | Returns | Purpose |
|--------|-----------|---------|---------|
| `showOverlay` | `()` | void | Show zone overlay |
| `hideOverlay` | `()` | void | Hide zone overlay |
| `isOverlayVisible` | `()` | `b` | Check visibility |
| `highlightZone` | `(s zoneId)` | void | Highlight single zone |
| `highlightZones` | `(as zoneIds)` | void | Highlight multiple zones |
| `clearHighlight` | `()` | void | Clear all highlights |
| `getPollIntervalMs` | `()` | `i` | Overlay poll interval |
| `getMinimumZoneSizePx` | `()` | `i` | Minimum zone size |
| `getMinimumZoneDisplaySizePx` | `()` | `i` | Minimum display size |
| `showShaderPreview` | `(i x, i y, i w, i h, s screenId, s shaderId, s paramsJson, s zonesJson)` | void | Show shader preview |
| `updateShaderPreview` | `(i x, i y, i w, i h, s paramsJson, s zonesJson)` | void | Update shader preview |
| `hideShaderPreview` | `()` | void | Hide shader preview |
| `showSnapAssist` | `(s screenId, s emptyZonesJson, s candidatesJson)` | `b success` | Show Snap Assist picker |
| `hideSnapAssist` | `()` | void | Hide Snap Assist |
| `isSnapAssistVisible` | `()` | `b` | Check Snap Assist visibility |
| `setSnapAssistThumbnail` | `(s kwinHandle, s dataUrl)` | void | Deliver window thumbnail |

### Signals

| Signal | Signature | Purpose |
|--------|-----------|---------|
| `overlayVisibilityChanged` | `(b visible)` | Overlay show/hide |
| `zoneHighlightChanged` | `(s zoneId)` | Highlighted zone changed |
| `snapAssistShown` | `(s screenId, s emptyZonesJson, s candidatesJson)` | Snap Assist shown |

---

## 4. org.plasmazones.Settings

Configuration registry, shader metadata, and running window enumeration.

### Methods

| Method | Signature | Returns | Purpose |
|--------|-----------|---------|---------|
| `reloadSettings` | `()` | void | Reload from disk |
| `saveSettings` | `()` | void | Save to disk |
| `resetToDefaults` | `()` | void | Reset all settings |
| `getAllSettings` | `()` | `s json` | All settings as JSON |
| `getSetting` | `(s key)` | `v variant` | Get single setting |
| `setSetting` | `(s key, v value)` | `b success` | Set single setting |
| `setSettings` | `(a{sv} settings)` | `b success` | Batch set settings |
| `getSettingKeys` | `()` | `as keys` | All setting key names |
| `setPerScreenSetting` | `(s screenId, s category, s key, v value)` | void | Per-screen setting |
| `clearPerScreenSettings` | `(s screenId, s category)` | void | Clear per-screen category |
| `getPerScreenSettings` | `(s screenId, s category)` | `a{sv}` | Get per-screen settings |
| `availableShaders` | `()` | `av` | Shader metadata list |
| `shaderInfo` | `(s shaderId)` | `a{sv}` | Shader metadata by ID |
| `defaultShaderParams` | `(s shaderId)` | `a{sv}` | Shader default parameters |
| `translateShaderParams` | `(s shaderId, a{sv} params)` | `a{sv}` | Translate param IDs to uniforms |
| `shadersEnabled` | `()` | `b` | Shaders compiled in? |
| `userShadersEnabled` | `()` | `b` | User shaders supported? |
| `userShaderDirectory` | `()` | `s path` | User shader directory |
| `openUserShaderDirectory` | `()` | void | Open in file manager |
| `refreshShaders` | `()` | void | Reload shader registry |
| `getRunningWindows` | `()` | `s json` | Running windows (blocks for effect) |
| `provideRunningWindows` | `(s json)` | void | Effect callback with windows |
| **Planned v1.1** | | | |
| `getSettingSchema` | `(s key)` | `s json` | Setting metadata (type, range, description) |
| `getAllSettingSchemas` | `()` | `s json` | All setting schemas |

### Signals

| Signal | Signature | Purpose |
|--------|-----------|---------|
| `settingsChanged` | `()` | Any setting modified |
| `runningWindowsRequested` | `()` | Request windows from effect |

---

## 5. org.plasmazones.Screen

Screen enumeration, metadata, and monitoring.

### Methods

| Method | Signature | Returns | Purpose |
|--------|-----------|---------|---------|
| `getScreens` | `()` | `as` | Screen connector names |
| `getScreenInfo` | `(s screenName)` | `s json` | Screen details (JSON) |
| `getPrimaryScreen` | `()` | `s` | Primary screen name |
| `getScreenId` | `(s screenName)` | `s` | Stable EDID-based screen ID |
| `setPrimaryScreenFromKWin` | `(s screenName)` | void | Set primary from KWin |

### Signals

| Signal | Signature | Purpose |
|--------|-----------|---------|
| `screenAdded` | `(s screenName)` | Screen connected |
| `screenRemoved` | `(s screenName)` | Screen disconnected |
| `screenGeometryChanged` | `(s screenName)` | Resolution/position changed |

---

## 6. org.plasmazones.WindowDrag

Window drag event handling from compositor bridge.

### Methods

| Method | Signature | Returns | Purpose |
|--------|-----------|---------|---------|
| `dragStarted` | `(s windowId, d x, d y, d w, d h, i mouseButtons)` | void | Drag began |
| `dragMoved` | `(s windowId, i cursorX, i cursorY, i modifiers, i mouseButtons)` | void | Cursor moved during drag |
| `dragStopped` | `(s windowId, i cursorX, i cursorY, i modifiers, i mouseButtons)` | `i snapX, i snapY, i snapW, i snapH, b shouldSnap, s releaseScreenId, b restoreSizeOnly, b snapAssistRequested, s emptyZonesJson` | Drag ended |
| `cancelSnap` | `()` | void | Cancel snap (Escape) |
| `handleWindowClosed` | `(s windowId)` | void | Window closed during drag |
| `selectorScrollWheel` | `(i angleDeltaY)` | void | Scroll zone selector |

### Signals

| Signal | Signature | Purpose |
|--------|-----------|---------|
| `zoneGeometryDuringDragChanged` | `(s windowId, i x, i y, i w, i h)` | Zone under cursor changed |
| `restoreSizeDuringDragChanged` | `(s windowId, i w, i h)` | Cursor left zones, restore size |

---

## 7. org.plasmazones.LayoutManager

Layout CRUD, per-screen/desktop/activity assignments, quick slots, editor launch.

*(See `dbus/org.plasmazones.LayoutManager.xml` for complete method listing — 54 methods)*

### Key Methods

| Method | Purpose |
|--------|---------|
| `getActiveLayout()` | Get active layout as JSON |
| `getLayoutList()` | List all layout IDs |
| `setActiveLayout(id)` | Switch active layout |
| `createLayout(name, type)` | Create new layout |
| `deleteLayout(id)` | Delete layout |
| `updateLayout(json)` | Update from editor |
| `assignLayoutToScreen(screenId, layoutId)` | Assign layout to screen |
| `setAssignmentEntry(screenId, desktop, activity, mode, layout, algorithm)` | Full assignment |
| `applyAssignmentChanges()` | Apply changes (resnap/retile) |
| `getScreenStates()` | All screen states as JSON |
| `openEditor()` | Launch layout editor |
| **Planned v1.1** | |
| `getCurrentVirtualDesktop()` | Current virtual desktop number |
| `getCurrentActivity()` | Current KDE Activity ID |

### Key Signals

| Signal | Purpose |
|--------|---------|
| `daemonReady()` | Daemon fully initialized |
| `layoutChanged(json)` | Active layout modified |
| `layoutListChanged()` | Layout list changed |
| `screenLayoutChanged(screenId, layoutId, desktop)` | Screen assignment changed |
| `assignmentChangesApplied()` | Resnap/retile completed |

---

## 8. org.plasmazones.ZoneDetection

Zone detection at coordinates, adjacency navigation, zone enumeration.

### Methods

| Method | Signature | Returns | Purpose |
|--------|-----------|---------|---------|
| `detectZoneAtPosition` | `(i x, i y)` | `s zoneId` | Zone at cursor position |
| `detectMultiZoneAtPosition` | `(i x, i y)` | `as zoneIds` | Multi-zone at position |
| `getZoneGeometry` | `(s zoneId)` | `s json` | Zone geometry as JSON |
| `getZoneGeometryForScreen` | `(s zoneId, s screenId)` | `s json` | Zone geometry for screen |
| `getZonesForScreen` | `(s screenId)` | `as zoneIds` | All zones on screen |
| `getAdjacentZone` | `(s zoneId, s direction)` | `s zoneId` | Adjacent zone (left/right/up/down) |
| `getFirstZoneInDirection` | `(s direction, s screenId)` | `s zoneId` | Edge zone in direction |
| `getZoneByNumber` | `(i number, s screenId)` | `s zoneId` | Zone by number (1-indexed) |
| `getAllZoneGeometries` | `(s screenId)` | `s json` | All zone geometries |
| `getKeyboardModifiers` | `()` | `i bitmask` | Current modifier state |

---

## Planned Interfaces

### 9. org.plasmazones.Shader (v1.1)

Dedicated shader management for the Shader Editor.

| Method/Signal | Signature | Purpose |
|---------------|-----------|---------|
| `assignShaderToZone(s zoneId, s shaderId, s paramsJson)` | Method | Per-zone shader assignment |
| `getShaderForZone(s zoneId)` | Method → `s json` | Get zone's shader+params |
| `clearShaderFromZone(s zoneId)` | Method | Remove zone shader |
| `getZoneShaderAssignments(s layoutId)` | Method → `s json` | All zone-shader pairs for layout |
| `shaderCompilationStarted(s shaderId)` | Signal | Compilation began |
| `shaderCompilationFinished(s shaderId, b success, s error)` | Signal | Compilation result |
| `userShaderAdded(s shaderId, s name, s path)` | Signal | New user shader detected |
| `userShaderRemoved(s shaderId)` | Signal | User shader deleted |

### 10. org.plasmazones.CompositorBridge (Experimental)

Compositor-agnostic window control protocol.

**Registration:**

| Method | Signature | Purpose |
|--------|-----------|---------|
| `registerBridge(s compositor, s version, as capabilities)` | → `s json` | Register compositor bridge |
| `reportModifierState(i modifiers, i mouseButtons)` | void | Report input state |

**Capabilities**: `borderless`, `maximize`, `border_rendering`, `modifier_tracking`, `animation`

**Commands (signals, daemon → bridge):**

| Signal | Signature | Purpose |
|--------|-----------|---------|
| `applyWindowGeometry(s windowId, i x, i y, i w, i h, s zoneId, b skipAnimation)` | Geometry command |
| `applyWindowGeometriesBatch(s batchJson)` | Batch geometry command |
| `activateWindow(s windowId)` | Focus command |
| `raiseWindows(as windowIds)` | Z-order command |
| `setWindowBorderless(s windowId, b borderless)` | Decoration command |
| `maximizeWindow(s windowId, i mode)` | Maximize command |
| `windowBorderRequested(s windowId, i x, i y, i w, i h, i borderW, i borderR, s activeColor, s inactiveColor, b isActive)` | Border rendering |
| `windowBorderRemoved(s windowId)` | Border removal |

### 11. org.plasmazones.Control (v1.1)

High-level convenience API for third-party integrations.

| Method/Signal | Signature | Purpose |
|---------------|-----------|---------|
| `getApiVersion()` | → `i` | Protocol version (1) |
| `getApiCapabilities()` | → `as` | Supported feature strings |
| `snapWindowToLayout(s windowId, s layoutId, i zoneNumber)` | One-call snap |
| `toggleAutotileForScreen(s screenId)` | Toggle autotile mode |
| `getFullState()` | → `s json` | Complete state snapshot |
| `subscribe(s eventPattern)` | → `s subscriptionId` | Event subscription |
| `unsubscribe(s subscriptionId)` | void | Cancel subscription |
| `event(s subscriptionId, s eventType, s eventJson)` | Signal | Filtered event stream |

---

## JSON Schemas

### Zone Geometry
```json
{"x": 0, "y": 32, "width": 1600, "height": 1716}
```

### Layout
```json
{
  "id": "uuid",
  "name": "Columns (2)",
  "zones": [
    {"id": "uuid", "x": 0.0, "y": 0.0, "width": 0.5, "height": 1.0, "zoneNumber": 1},
    {"id": "uuid", "x": 0.5, "y": 0.0, "width": 0.5, "height": 1.0, "zoneNumber": 2}
  ]
}
```

### Tile Request Batch (windowsTileRequested)
```json
[
  {"windowId": "app|uuid", "x": 0, "y": 32, "width": 1600, "height": 858, "monocle": false, "floating": false, "screenId": "..."},
  {"windowId": "app|uuid", "floating": true, "screenId": "..."}
]
```

### Batch Snap (applyGeometriesBatch)
```json
[
  {"windowId": "app|uuid", "targetZoneId": "uuid", "sourceZoneId": "uuid", "x": 0, "y": 32, "width": 1600, "height": 858}
]
```

### Window State (v1.1 getWindowState)
```json
{
  "windowId": "app|uuid",
  "zoneId": "uuid",
  "screenId": "manufacturer:model:serial",
  "isFloating": false,
  "isAutotiled": true,
  "geometry": {"x": 0, "y": 32, "width": 1600, "height": 858}
}
```

### Snap Assist Candidates
```json
[
  {"windowId": "app|uuid", "kwinHandle": "uuid", "icon": "firefox", "caption": "Mozilla Firefox", "iconPng": "data:image/png;base64,..."}
]
```

### Setting Schema (v1.1 getSettingSchema)
```json
{
  "type": "int",
  "min": 0,
  "max": 50,
  "default": 8,
  "description": "Gap between tiled windows in pixels",
  "enumValues": null
}
```

### Screen Info (getScreenInfo)
```json
{
  "name": "DP-2",
  "manufacturer": "LG Electronics",
  "model": "LG Ultra HD",
  "serial": "115107",
  "screenId": "LG Electronics:LG Ultra HD:115107",
  "geometry": {"x": 0, "y": 0, "width": 3200, "height": 1800},
  "scale": 1.0,
  "refreshRate": 60.0
}
```
