# Wayfire Plugin for PlasmaZones — Implementation Plan

## Context

PlasmaZones is a zone-based window tiling manager for KDE Plasma. Its architecture cleanly separates compositor integration (KWin effect plugin, ~7,400 lines) from business logic (daemon, ~everything else). The daemon, layer-shell overlays, editor, and settings are already compositor-agnostic.

This plan creates a **Wayfire compositor plugin** that replaces the KWin effect, speaking the same D-Bus protocol to the unchanged daemon. The existing layer-shell overlays, editor, and settings app work on Wayfire without modification.

## Architecture Decision: Hybrid Plugin

A thin Wayfire C++ plugin acts as sensor/actuator (like the KWin effect):
- **Sensor**: detects window drags, modifier keys, window lifecycle, screen changes
- **Actuator**: applies geometry, activates windows, manages animations/borders
- **Bridge**: all logic delegated to daemon via D-Bus (same protocol as KWin effect)

## File Structure

```
wayfire-plugin/
├── CMakeLists.txt                 (~80 lines)   Build config
├── metadata.ini                   (~15 lines)   Wayfire plugin descriptor
├── plasmazones.cpp                (~1,400 lines) Plugin entry + main wiring
├── plasmazones.hpp                (~250 lines)   Main header
├── dbus_bridge.cpp/hpp            (~520 lines)   D-Bus send/receive layer
├── dbus_constants.hpp             (~30 lines)    Service/interface strings (verbatim copy)
├── drag_tracker.cpp/hpp           (~250 lines)   core_drag_t signal → D-Bus
├── modifier_tracker.cpp/hpp       (~210 lines)   wlr_keyboard modifiers + trigger gating
├── window_animator.cpp/hpp        (~470 lines)   Snap animation via view_2d_transformer_t
├── easing_curve.cpp/hpp           (~290 lines)   Bezier/elastic/bounce (verbatim copy)
├── autotile_handler.cpp/hpp       (~950 lines)   Window lifecycle for autotile D-Bus
├── screen_handler.cpp/hpp         (~310 lines)   Output changes + EDID screen ID
├── snap_assist_handler.cpp/hpp    (~210 lines)   Candidate building for snap assist overlay
├── window_utils.cpp/hpp           (~280 lines)   Window ID, filtering, helpers
└── border_renderer.cpp/hpp        (~380 lines)   wlr_scene_rect border rendering
```

**Estimated total: ~5,600 lines** (vs 7,400 for KWin effect — reduction from cleaner drag API and transformer pattern)

## Build System

### New CMake option (top-level CMakeLists.txt)
```cmake
option(USE_WAYFIRE "Build Wayfire compositor plugin" OFF)
# Mutually exclusive with USE_KDE_FRAMEWORKS for the compositor plugin
if(USE_WAYFIRE)
    add_subdirectory(wayfire-plugin)
endif()
```

### wayfire-plugin/CMakeLists.txt dependencies
- `wayfire` (via pkg-config) — pulls in wlroots
- `Qt6::Core`, `Qt6::DBus` — for D-Bus bridge (same as KWin effect)
- NOT linked to `plasmazones_core` — standalone plugin (same pattern as KWin effect)

### Plugin installation
- `libplasmazones.so` → `${WAYFIRE_PLUGIN_DIR}` (from pkg-config)
- `metadata.ini` → same directory

### User activation (wayfire.ini)
```ini
[core]
plugins = ... plasmazones
```

## Key API Mappings

### Drag Detection (replaces DragTracker)
| KWin | Wayfire |
|------|---------|
| `windowStartUserMovedResized` signal | `drag_focus_output_signal` on `core_drag_t` |
| `isUserMove()` polling | `drag_helper->view != nullptr` |
| `mouseChanged` for cursor pos during drag | `drag_motion_signal` with `current_position` |
| `windowFinishUserMovedResized` / `forceEnd` | `drag_done_signal` with `main_view`, `grab_position` |
| 30Hz throttle via `QElapsedTimer` | Same `QElapsedTimer` pattern (Qt available in plugin) |

### Window API (replaces EffectWindow)
| KWin | Wayfire |
|------|---------|
| `w->frameGeometry()` | `view->get_geometry()` |
| `w->moveResize(geo)` / `window()->moveResize()` | `view->set_geometry(geo)` |
| `w->windowClass()` | `view->get_app_id()` |
| `w->caption()` | `view->get_title()` |
| `w->internalId()` | `view->get_id()` (uint32_t) |
| `w->pid()` | `view->get_client_pid()` |
| `w->isMinimized()` | `view->minimized` |
| `w->isFullScreen()` | `view->toplevel()->current().fullscreen` |
| `w->isSpecialWindow()/isDesktop()/isDock()` | `view->role != VIEW_ROLE_TOPLEVEL` |
| `w->isMovable()` | `view->get_allowed_actions() & VIEW_ALLOW_MOVE` |
| `w->isOnCurrentDesktop()` | Check workspace via `view->get_wset_index()` |
| `w->screen()` | `view->get_output()` |
| `w->expandedGeometry()` | `view->get_bounding_box()` |
| `effects->stackingOrder()` | `wf::get_core().get_all_views()` |
| `effects->activeWindow()` | `wf::get_core().seat->get_active_view()` |
| `effects->activateWindow(w)` | `wf::get_core().focus_view(view)` |
| `effects->cursorPos()` | `wf::get_core().get_cursor_position()` |
| `effects->addRepaint()` | `output->render->schedule_redraw()` |

### Modifier Keys (replaces mouseChanged modifier tracking)
```cpp
auto kb = wlr_seat_get_keyboard(wf::get_core().get_current_seat());
uint32_t mods = wlr_keyboard_get_modifiers(kb);
// Map WLR_MODIFIER_SHIFT/CTRL/ALT/LOGO to Qt::KeyboardModifiers for trigger check
```

### Animation (replaces paintWindow + WindowPaintData)
| KWin | Wayfire |
|------|---------|
| `prePaintScreen(presentTime)` | `output->render->add_effect()` callback |
| `data += QPointF(dx, dy)` | `transformer->translation_x/y` |
| `data.setXScale(sx)` | `transformer->scale_x` |
| `data.multiplyOpacity(a)` | `transformer->alpha` |
| `window->addRepaintFull()` | `output->render->schedule_redraw()` |
| Attach: implicit (paintWindow override) | `view->get_transformed_node()->add_transformer(...)` |
| Detach: animation complete | `view->get_transformed_node()->rem_transformer("pz-snap")` |

### Borders (replaces OutlinedBorderItem)
Use `wlr_scene_rect` nodes (4 per window: top/bottom/left/right) attached as siblings in the scene graph. Active/inactive color via `wlr_scene_rect_set_color()`. Repositioned on `view_geometry_changed_signal`.

### Screen ID (replaces outputScreenId)
```cpp
// wlr_output gives make/model/serial directly
wlr_output* wlr = output->handle;
QString manufacturer = QString::fromUtf8(wlr->make);
QString model = QString::fromUtf8(wlr->model);
QString serial = QString::fromUtf8(wlr->serial);
// Use shared ScreenIdUtils for hex normalization + sysfs EDID fallback
QString baseId = ScreenIdUtils::buildScreenBaseId(manufacturer, model, serial, connectorName);
// + duplicate disambiguation (same pattern as daemon/KWin effect)
```
Reference: `src/compositor-common/screen_id.h` — `buildScreenBaseId()`, `readEdidHeaderSerial()`

### Window ID (replaces getWindowId)
```cpp
QString appId = QString::fromStdString(view->get_app_id()).toLower();
QString instanceId = QString::number(view->get_id());
return appId + QLatin1Char('|') + instanceId;
// Daemon uses portion after '|' as opaque disambiguator — uint32_t string is fine
```

### Keyboard Grab (replaces grabbedKeyboardEvent)
Wayfire: use `wf::get_core().add_activator(...)` or input grab during drag for Escape key handling. Alternative: check key state in drag_motion callback.

## Vulkan Support

Wayfire **does** support Vulkan rendering via wlroots' `wlr_vk_renderer` ([WayfireWM/wayfire#2941](https://github.com/WayfireWM/wayfire/issues/2941)). Plugins can access `VkDevice`/`VkQueue`/`VkCommandBuffer` via `wlr_vk_renderer_get_*()` and write custom Vulkan shaders through post-processing hooks (`add_post`). Currently verbose (~500+ lines boilerplate per effect), but helper utilities are proposed that would halve the code. This means compositor-side Vulkan effects (open/close shaders, post-processing) are possible on Wayfire, not just client-side via Qt RHI.

For initial implementation: use GL-based `view_2d_transformer_t` for snap animations. Vulkan shader support can be added later when the helpers land or for advanced effects.

## D-Bus Protocol Contract

The Wayfire plugin speaks the exact same D-Bus protocol as the KWin effect. The daemon requires no changes.

### Service Identity
```
Service: org.plasmazones
Object Path: /PlasmaZones
```

### Critical Methods (Plugin → Daemon)

**WindowDrag interface** (`org.plasmazones.WindowDrag`):
| Method | Arguments | Returns |
|--------|-----------|---------|
| `dragStarted` | `(s windowId, d x, d y, d w, d h, i mouseButtons)` | void |
| `dragMoved` | `(s windowId, i cursorX, i cursorY, i modifiers, i mouseButtons)` | void |
| `dragStopped` | `(s windowId, i cursorX, i cursorY, i mods, i buttons)` | `(i snapX, i snapY, i snapW, i snapH, b shouldSnap, s screenName, b restoreSizeOnly, b snapAssistRequested, EmptyZoneList emptyZones)` |
| `cancelSnap` | — | void |

**WindowTracking interface** (`org.plasmazones.WindowTracking`):
| Method | Arguments | Returns |
|--------|-----------|---------|
| `windowActivated` | `(s windowId, s screenName)` | void |
| `windowClosed` | `(s windowId)` | void |
| `windowSnapped` | `(s windowId, s zoneId, s screenName)` | void |
| `storePreTileGeometry` | `(s windowId, i x, i y, i w, i h, s screenName, b overwrite)` | void |
| `setWindowFloating` | `(s windowId, b floating)` | void |
| `resolveWindowRestore` | `(s windowId, s screenName, b sticky)` | `(i x, i y, i w, i h, b shouldSnap)` |
| `snapToAppRule` | `(s windowId, s screenName, b sticky)` | `(i x, i y, i w, i h, b shouldSnap)` |
| `snapToLastZone` | `(s windowId, s screenName, b sticky)` | `(i x, i y, i w, i h, b shouldSnap)` |

**Autotile interface** (`org.plasmazones.Autotile`):
| Method | Arguments | Returns |
|--------|-----------|---------|
| `windowOpened` | `(s windowId, s screenName, i minW, i minH)` | void |
| `windowClosed` | `(s windowId)` | void |
| `windowsOpenedBatch` | `(WindowOpenedList batch)` | void |
| `notifyWindowFocused` | `(s windowId, s screenName)` | void |

**Overlay interface** (`org.plasmazones.Overlay`):
| Method | Arguments | Returns |
|--------|-----------|---------|
| `showSnapAssist` | `(s screenName, EmptyZoneList emptyZones, SnapAssistCandidateList candidates)` | `b success` |
| `hideSnapAssist` | — | void |

### Critical Signals (Daemon → Plugin)

**WindowTracking interface:**
| Signal | Arguments | Plugin Action |
|--------|-----------|---------------|
| `applyGeometryRequested` | `(s windowId, i x, i y, i width, i height, s zoneId, s screenId, b sizeOnly)` | `view->set_geometry()` + animate |
| `applyGeometriesBatch` | `(a(siiii) geometries, s action)` | Batch geometry apply |
| `activateWindowRequested` | `(s windowId)` | `focus_view()` |
| `toggleWindowFloatRequested` | `(b shouldFloat)` | Toggle active window float |
| `raiseWindowsRequested` | `(as windowIds)` | Restack views |
| `windowFloatingChanged` | `(s stableId, b isFloating, s screenId)` | Update float cache |
| `restoreSizeDuringDragChanged` | `(s windowId, i w, i h)` | Apply size during drag |
| `moveSpecificWindowToZoneRequested` | `(s windowId, s zoneId, i x, i y, i width, i height)` | Move window to zone |

**Autotile interface:**
| Signal | Arguments | Plugin Action |
|--------|-----------|---------------|
| `windowsTileRequested` | `(a(siiiissbb) tileRequests)` | Batch `set_geometry()` |
| `focusWindowRequested` | `(s windowId)` | `focus_view()` |
| `enabledChanged` | `(b enabled)` | Update autotile state |
| `autotileScreensChanged` | `(as screenNames, b isDesktopSwitch)` | Refresh screen tracking |
| `windowFloatingChanged` | `(s windowId, b isFloating, s screenId)` | Float cleanup |

### Data Formats

**Window ID:** `"appId|instanceId"` — e.g., `"firefox|42"`
- `appId` = Wayland `app_id` (lowercased)
- `instanceId` = `view->get_id()` as string (opaque disambiguator)

**Screen ID:** `"manufacturer:model:serial"` — e.g., `"Samsung:LU28E590DS:2919876"`
- From `wlr_output->make/model/serial` + hex normalization + sysfs EDID fallback
- Must match `src/core/utils.cpp` `screenIdentifier()` byte-for-byte

**D-Bus struct types** (18 types, registered via `PlasmaZones::registerDBusTypes()` at startup):
- `WindowGeometryEntry (siiii)` — windowId, x, y, width, height
- `TileRequestEntry (siiiissbb)` — windowId, x, y, width, height, zoneId, screenId, monocle, floating
- `SnapAllResultEntry (sssiiii)` — windowId, targetZoneId, sourceZoneId, x, y, width, height
- `SnapConfirmationEntry (sssb)` — windowId, zoneId, screenId, isRestore
- `WindowOpenedEntry (ssii)` — windowId, screenId, minWidth, minHeight
- `WindowStateEntry (sssbs)` — windowId, zoneId, screenId, isFloating, changeType
- `UnfloatRestoreResult (bassiiii)` — found, zoneIds, screenName, x, y, width, height
- `ZoneGeometryRect (iiii)` — x, y, width, height
- `EmptyZoneEntry (siiiiii)` — zoneId, x, y, width, height, borderWidth, borderRadius
- `SnapAssistCandidate (ssss)` — windowId, compositorHandle, icon, caption
- `NamedZoneGeometry (siiii)` — zoneId, x, y, width, height
- `AlgorithmInfoEntry (sssbbbbdibsbb)` — id, name, description, supportsMasterCount, supportsSplitRatio, centerLayout, producesOverlappingZones, defaultSplitRatio, defaultMaxWindows, isScripted, zoneNumberDisplay, isUserScript, supportsMemory
- `BridgeRegistrationResult (sss)` — apiVersion, bridgeName, sessionId
- `MoveTargetResult (bssiiiiss)` — success, reason, zoneId, x, y, width, height, sourceZoneId, screenName
- `FocusTargetResult (bsssss)` — success, reason, windowIdToActivate, sourceZoneId, targetZoneId, screenName
- `CycleTargetResult (bssss)` — success, reason, windowIdToActivate, zoneId, screenName
- `SwapTargetResult (bssiiiissiiiissss)` — success, reason, windowId1, x1, y1, w1, h1, zoneId1, windowId2, x2, y2, w2, h2, zoneId2, screenName, sourceZoneId, targetZoneId
- `RestoreTargetResult (bbiiii)` — success, found, x, y, width, height

**Snap assist candidates:** `SnapAssistCandidateList` — a typed D-Bus array of `SnapAssistCandidate (ssss)` structs (windowId, compositorHandle, icon, caption)

## Implementation Phases

### Phase 1: Skeleton + D-Bus Bridge (~2,100 lines)
**Files:** `plasmazones.cpp/hpp`, `dbus_bridge.cpp/hpp`, `dbus_constants.hpp`, `window_utils.cpp/hpp`, `CMakeLists.txt`, `metadata.ini`

**Goal:** Plugin loads in Wayfire, connects to daemon D-Bus, reports window activations.

**Key tasks:**
- `wf::plugin_interface_t` subclass with `init()` / `fini()`
- D-Bus connection + `QDBusServiceWatcher` for daemon registration
- `fireAndForgetDBusCall()` and `asyncMethodCall()` (direct port)
- `getWindowId(wayfire_view)` — window ID generation
- `getScreenId(wf::output_t*)` — EDID screen ID from `wlr_output`
- `shouldHandleWindow(wayfire_view)` — window type filtering
- `view_mapped_signal` / `view_unmapped_signal` / `view_focused_signal` connections
- `loadCachedSettings()` async D-Bus pattern

**Test:** Load plugin, `dbus-monitor` shows `windowActivated` calls with correct IDs.

### Phase 2: Drag Detection + Modifier Gating (~460 lines)
**Files:** `drag_tracker.cpp/hpp`, `modifier_tracker.cpp/hpp`

**Goal:** Full drag lifecycle forwarded to daemon via D-Bus.

**Key tasks:**
- Obtain `core_drag_t` via `wf::shared_data::ref_ptr_t`
- Connect `drag_focus_output_signal` → `dragStarted` D-Bus (deferred)
- Connect `drag_motion_signal` → `dragMoved` D-Bus (30Hz throttled)
- Connect `drag_done_signal` → `dragStopped` D-Bus → apply snap geometry
- `wlr_keyboard_get_modifiers()` → `ParsedTrigger` gating
- Deferred dragStarted pattern (only on trigger detection)

**Test:** Drag with modifier → overlay appears. Drop → window snaps. No modifier → nothing.

### Phase 3: Geometry Application + Screen Changes (~310 lines)
**Files:** `screen_handler.cpp/hpp`, updates to `plasmazones.cpp`

**Goal:** Daemon-driven geometry commands work.

**Key tasks:**
- `applyGeometryRequested` signal → `view->set_geometry()`
- `applyGeometriesBatch` signal → batch geometry with stagger
- `activateWindowRequested` signal → `focus_view()`
- `output_layout_changed` signal → debounce → `fetchAndApplyWindowGeometries()`
- Screen ID cache (connector → EDID ID)

**Test:** Keyboard navigation (Meta+Arrow) moves windows. Monitor hotplug repositions.

### Phase 4: Snap Animations (~760 lines)
**Files:** `window_animator.cpp/hpp`, `easing_curve.cpp/hpp`

**Goal:** Smooth visual transitions when snapping.

**Key tasks:**
- Copy `EasingCurve` verbatim (zero dependencies)
- Copy `WindowAnimation` struct verbatim
- Create `view_2d_transformer_t` subclass for snap animation
- `output->render->add_effect()` frame callback to advance animations
- Attach transformer on snap, remove on completion
- Mid-flight redirect (new snap during animation)

**Test:** Snap → smooth slide. Elastic/bounce easing works. Settings change reflected.

### Phase 5: Autotile Integration (~950 lines)
**Files:** `autotile_handler.cpp/hpp`

**Goal:** Full autotile window lifecycle.

**Key tasks:**
- `view_mapped_signal` → `Autotile.windowOpened` D-Bus
- `view_unmapped_signal` → `Autotile.windowClosed` D-Bus
- `view_focused_signal` → `Autotile.notifyWindowFocused`
- `windowsTileRequested` signal → batch set_geometry with stagger
- `focusWindowRequested` signal → `focus_view()`
- Monocle mode (maximize/restore)
- Title bar hiding via server-side decoration control
- Focus-follows-mouse on autotile screens
- Per-screen autotile state tracking
- Minimize/maximize/fullscreen state change handling

**Test:** Enable autotile → windows tile. Open/close retiles. All autotile features work.

### Phase 6: Snap Assist + Navigation Polish (~210 lines + updates)
**Files:** `snap_assist_handler.cpp/hpp`, updates to `plasmazones.cpp`

**Goal:** Complete snap assist and remaining features.

**Key tasks:**
- `buildCandidates()` — collect unsnapped windows for snap assist
- `showSnapAssist` D-Bus call with `EmptyZoneList` + `SnapAssistCandidateList` typed structs
- Float toggle (`toggleWindowFloatRequested` signal)
- `raiseWindowsRequested` signal → restack views
- `snapAllWindowsRequested` signal → collect + send to daemon
- Navigation feedback D-Bus signals

**Test:** Snap → assist popup → select candidate. Float toggle works.

### Phase 7: Border Rendering (~380 lines)
**Files:** `border_renderer.cpp/hpp`

**Goal:** Colored borders on tiled/borderless windows.

**Key tasks:**
- 4x `wlr_scene_rect` per window (top/bottom/left/right)
- Track in `QHash<view_id, WindowBorder>`
- Reposition on `view_geometry_changed_signal`
- Active/inactive color from autotile settings
- Border width and radius from settings
- Remove on window close or float

**Test:** Enable borders → colored outlines. Active/inactive colors differ. Resize updates.

## Shared Library: `plasmazones_compositor_common`

Portable code has been extracted to `src/compositor-common/` as a static library.
The Wayfire plugin should **link against this library** instead of copying code from `kwin-effect/`.

### Available modules (link via `target_link_libraries(... plasmazones_compositor_common)`)

| Header | Contents |
|--------|----------|
| `easingcurve.h` | `EasingCurve` struct + all math, `WindowAnimation` struct, `AnimationConfig` |
| `animation_math.h` | `createSnapAnimation()`, `computeOvershootBounds()` |
| `autotile_state.h` | `BorderState` struct, `AutotileStateHelpers` (border accessors, geometry helpers, cleanup) |
| `compositor_bridge.h` | `ICompositorBridge` interface, `WindowHandle`, `WindowInfo` |
| `dbus_constants.h` | D-Bus service/interface string constants |
| `dbus_helpers.h` | `fireAndForget()`, `asyncCall()`, `loadSettingAsync()` |
| `debounced_action.h` | `DebouncedScreenAction` — screen change debounce + coalescing |
| `floating_cache.h` | `FloatingCache` — floating window state with appId fallback |
| `dbus_types.h` | All 18 D-Bus struct types + list aliases + `registerDBusTypes()` (see D-Bus struct types list above) |
| `screen_id.h/cpp` | `readEdidHeaderSerial()`, `normalizeHexSerial()`, `buildScreenBaseId()` — EDID parsing + screen ID |
| `snap_assist_filter.h` | `buildCandidates()` — compositor-agnostic snap assist filtering (returns `SnapAssistCandidateList`) |
| `stagger_timer.h` | `applyStaggeredOrImmediate()` — cascading animation timer |
| `trigger_parser.h` | `ParsedTrigger`, `checkModifier()`, `anyTriggerHeld()`, `parseTriggers()` |
| `window_id.h` | `extractAppId()`, `appIdMatches()`, `deriveShortName()`, `snapToRect()`, `iconToDataUrl()` |

### Wayfire plugin CMake integration

```cmake
# wayfire-plugin/CMakeLists.txt
target_link_libraries(plasmazones_wayfire PRIVATE
    plasmazones_compositor_common  # Shared animation, D-Bus, parsing code
    Qt6::Core
    Qt6::DBus
)
```

### Wayfire plugin implementation pattern

The Wayfire plugin implements `ICompositorBridge` with Wayfire-specific window APIs,
then passes it to the shared functions. Example:

```cpp
// wayfire_compositor_bridge.cpp
WindowHandle WayfireCompositorBridge::findWindowById(const QString& windowId) const {
    // Iterate wayfire_view list, match by app_id + view_id
}

// In drag handler:
SnapAssistCandidateList candidates = SnapAssistFilter::buildCandidates(m_bridge, excludeId, screenId, snappedIds);
```

### Completed extractions (this PR)

- **Screen ID algorithm** — `ScreenIdUtils` in `screen_id.h/cpp`: EDID sysfs parsing, hex normalization, `buildScreenBaseId()`. Daemon and KWin effect both delegate to shared code.
- **Window filtering predicates** — `appIdMatches()` in `window_id.h`: segment-aware app ID matching for exclusion lists.
- **Snap assist filter** — `buildCandidates()` returns `SnapAssistCandidateList` directly (no JSON intermediate).
- **`extractAppId`** — canonical implementation in `window_id.h`, `Utils::extractAppId` delegates.
- **`snapToRect`** — edge-consistent QRectF→QRect rounding in `window_id.h`, `GeometryUtils::snapToRect` delegates.

### Remaining extraction candidates (lower priority)

- Focus-follows-mouse hit testing — mostly compositor-specific (iterates stacking order via `ICompositorBridge`), core loop is small
- Drag state machine (throttle + deferred activation) — `DragTracker` uses KWin types; pure state logic is minimal and well-isolated

## Critical Reference Files

| File | Why |
|------|-----|
| `kwin-effect/plasmazoneseffect.cpp` (3,112 lines) | All D-Bus wiring, window filtering, geometry application |
| `kwin-effect/plasmazoneseffect.h` (525 lines) | Full API surface and state |
| `kwin-effect/windowanimator.cpp` (475 lines) | Animation system to port |
| `kwin-effect/autotilehandler.h` (268 lines) | Autotile state machine interface |
| `kwin-effect/autotilehandler/signals.cpp` (559 lines) | D-Bus signal handling patterns |
| `kwin-effect/autotilehandler/tiling.cpp` (460 lines) | Geometry application patterns |
| `kwin-effect/dragtracker.cpp` (124 lines) | Drag semantics to replicate |
| `src/core/utils.cpp` | Screen ID generation (must match byte-for-byte) |
| `dbus/org.plasmazones.WindowDrag.xml` | Drag D-Bus contract |
| `dbus/org.plasmazones.WindowTracking.xml` | Window tracking D-Bus contract |
| `dbus/org.plasmazones.Autotile.xml` | Autotile D-Bus contract |
| `dbus/org.plasmazones.Overlay.xml` | Overlay D-Bus contract |

## Verification

### Per-phase testing
Each phase has a clear functional test (see phase descriptions above).

### Full integration test
1. Build with `-DUSE_WAYFIRE=ON -DUSE_KDE_FRAMEWORKS=OFF`
2. Start daemon: `plasmazonesd`
3. Start Wayfire with plugin enabled in `wayfire.ini`
4. Test: drag-to-snap, keyboard navigation, autotile, snap assist, animations, borders
5. Monitor D-Bus: `dbus-monitor --session "interface='org.plasmazones'"`
6. Compare behavior against KWin effect on identical layout configurations

### Automated tests
- `wayfire-plugin/tests/` with Google Test
- Mock `wf::view_interface_t` for window ID/filtering tests
- Mock D-Bus for signal handling tests
- Wayfire headless backend (`WLR_BACKENDS=headless`) for integration tests
