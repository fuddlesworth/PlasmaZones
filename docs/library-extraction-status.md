# Library Extraction Status

Current state of extracting PlasmaZones into reusable LGPL shared libraries
for compositor/WM/shell independence.

## Extracted Libraries (20)

All live under `libs/`, are SHARED libraries with CMake install + config packages.

### Foundation Layer

| Library | Purpose | Deps |
|---------|---------|------|
| `phosphor-geometry` | Pure math — coordinate transforms, overlap resolution, min-size enforcement, JSON serialization | Qt6::Core |
| `phosphor-identity` | Screen identity (EDID, connector naming) | Qt6::Core |
| `phosphor-protocol` | Wire types for IPC (WindowStateEntry, WindowGeometryList, etc.) | Qt6::Core |
| `phosphor-fsloader` | Filesystem layout/config loading | Qt6::Core |

### Engine Layer

| Library | Purpose | Deps |
|---------|---------|------|
| `phosphor-engine-api` | Engine contracts: IPlacementEngine, IPlacementState, IWindowTrackingService, IVirtualDesktopManager, IWindowRegistry, PlacementEngineBase, GeometryUtils, NavigationContext | Qt6::Core/Gui, phosphor-geometry |
| `phosphor-zones` | Zone data: Layout, Zone, IZoneDetector, ILayoutManager, LayoutRegistry, AssignmentEntry | Qt6::Core, phosphor-engine-api |
| `phosphor-tiles` | Tiling algorithms (tree-based layout) | Qt6::Core, phosphor-engine-api |
| `phosphor-snap-engine` | SnapEngine — complete zone-based snap placement | Qt6::Core/Gui, phosphor-engine-api, phosphor-zones, phosphor-protocol, phosphor-screens, phosphor-identity |
| `phosphor-tile-engine` | AutotileEngine — autotile placement with config/nav/overflow | Qt6::Core, phosphor-engine-api, phosphor-tiles, phosphor-screens, phosphor-identity, phosphor-zones |
| `phosphor-layout-api` | Layout source abstraction: ILayoutSource, CompositeLayoutSource, LayoutPreview, AlgorithmMetadata | Qt6::Core |
| `phosphor-screens` | Screen manager, virtual screen swapping, screen topology | Qt6::Core/Gui |

### Compositor/Shell Layer

| Library | Purpose | Deps |
|---------|---------|------|
| `phosphor-wayland` | Wayland protocol wrappers: LayerSurface, ToplevelDrag, IdleNotifier, SinglePixelBuffer | Qt6::Core/Gui, wayland-client |
| `phosphor-layer` | Shell surface management: Surface, SurfaceFactory, TopologyCoordinator, Role, ISurfaceAnimator, IScreenProvider | Qt6::Core/Gui/Quick |
| `phosphor-surfaces` | Higher-level SurfaceManager orchestration | Qt6::Core, phosphor-layer |
| `phosphor-animation` | Surface animation (QML shaders, transitions) | Qt6::Core/Quick |
| `phosphor-rendering` | GPU rendering: ShaderCompiler, ShaderEffect, ShaderNodeRhi, ZoneShaderCommon | Qt6::Core/Gui |
| `phosphor-shaders` | Shader pack assets (26 packs with previews) | — |

### App/Config Layer

| Library | Purpose | Deps |
|---------|---------|------|
| `phosphor-config` | Config file management | Qt6::Core |
| `phosphor-shortcuts` | Shortcut registry and factory | Qt6::Core |
| `phosphor-audio` | Audio feedback | Qt6::Core |

---

## Remaining in Daemon Binary (~62,700 lines)

### `src/core/` — 13,695 lines

Extraction candidates (library-grade code still compiled into the daemon):

| Component | Lines | Current Deps | Target Library | Blocker |
|-----------|-------|--------------|----------------|---------|
| WindowTrackingService | 3,033 | IWindowTrackingService (engine-api), IZoneDetector (zones), SnapState (snap-engine), ScreenManager (screens), PlacementEngineBase | New: `phosphor-wts` | Depends on VDM, WindowRegistry, core types |
| GeometryUtils (daemon-side) | 1,115 | Qt6::Core/Gui, core/types.h | Merge into: `phosphor-geometry` | Need to lift types.h deps |
| VirtualDesktopManager | 491 | IVirtualDesktopManager (engine-api), Qt D-Bus, KActivities | New: `phosphor-vdm` or merge into engine-api | D-Bus/KActivities coupling |
| WindowRegistry | 392 | IWindowRegistry (engine-api) | Merge into: `phosphor-engine-api` | Trivial |
| ScreenModeRouter | 163 | IPlacementEngine (engine-api), AssignmentEntry (zones) | Merge into: `phosphor-engine-api` | Trivial |
| LayoutFactory + LayoutWorker | ~600 | PhosphorZones, PhosphorLayoutApi | Merge into: `phosphor-layout-api` | None |
| UnifiedLayoutList | ~300 | PhosphorZones | Merge into: `phosphor-layout-api` | None |
| ActivityManager | ~200 | D-Bus (KActivities) | Probably stays — KDE-specific | KActivities dep |
| Platform / Logging / Enums / Types / Settings interfaces | ~2,000 | Qt6 | Partially extractable | Some are daemon-specific glue |
| AnimationBootstrap / ShaderRegistry | ~300 | phosphor-animation, phosphor-rendering | Could merge into those libs | None |
| Misc (translation, single-instance, support report, modifierutils) | ~500 | Qt6 | Stays — daemon infrastructure | — |

### `src/daemon/` — 24,830 lines

Shell/application layer (what the daemon actually IS):

| Component | Lines | Library candidate? |
|-----------|-------|--------------------|
| OverlayService (zone overlays, OSD, snap-assist, selectors) | 7,376 | Maybe → `phosphor-overlay`. Already uses phosphor-layer + phosphor-surfaces. |
| ZoneShaderItem + rendering pipeline | ~2,000 | Could merge into `phosphor-rendering` |
| ShortcutManager | ~500 | Could merge into `phosphor-shortcuts` |
| UnifiedLayoutController | ~500 | Could merge into `phosphor-layout-api` |
| ZoneSelectorController | ~500 | Tied to overlay — goes with it |
| SnapAssistThumbnailProvider | ~200 | Stays — QQuickImageProvider subclass |
| Daemon class (wiring, lifecycle, start, signals) | ~5,000 | No — this IS the shell binary |
| EngineFactory | ~100 | No — shell startup code |
| ModeTracker | ~300 | Stays — daemon state |
| VulkanSupport | ~200 | Could merge into phosphor-rendering |
| main.cpp | ~100 | No |

### `src/dbus/` — 24,201 lines

D-Bus service adaptors (IPC surface for external consumers):

| Adaptor | Lines | Notes |
|---------|-------|-------|
| WindowTrackingAdaptor | ~5,000 | Largest — persistence, float, navigation, convenience |
| WindowDragAdaptor | ~2,500 | Drag protocol impl — reusable if another process does drag |
| SnapAdaptor | ~1,500 | Engine-specific D-Bus surface |
| AutotileAdaptor | ~1,000 | Engine-specific D-Bus surface |
| LayoutAdaptor | ~1,000 | Assignment + editor |
| Others (control, compositor-bridge, overlay, screen, settings, shader, zone-detection) | ~3,000 | General daemon IPC |

Not library candidates in the traditional sense — they're the daemon's public API.

---

## Extraction Order (dependency-driven)

### Phase 1 — Low-hanging fruit (no new libraries needed)

1. **Merge daemon-side GeometryUtils → `phosphor-geometry`**
   - Files: `src/core/geometryutils.{h,cpp}`, `src/core/geometryutils/constraints.cpp`, `overlaps.cpp`
   - ~1,115 lines of pure constraint/overlap math
   - Requires lifting any `core/types.h` dependencies

2. **Merge ScreenModeRouter → `phosphor-engine-api`**
   - Files: `src/core/screenmoderouter.{h,cpp}`
   - 163 lines, deps already satisfied

3. **Merge WindowRegistry → `phosphor-engine-api`**
   - Files: `src/core/windowregistry.{h,cpp}`
   - 392 lines, implements IWindowRegistry

4. **Merge LayoutFactory + LayoutWorker + UnifiedLayoutList → `phosphor-layout-api`**
   - ~900 lines, deps already satisfied

### Phase 2 — New library for VDM

5. **Extract VirtualDesktopManager → new `phosphor-vdm`** (or engine-api if D-Bus dep is acceptable)
   - 491 lines
   - Implements IVirtualDesktopManager
   - Has D-Bus + KActivities dep — may need to be its own lib to keep engine-api pure

### Phase 3 — WTS extraction (biggest value)

6. **Extract WindowTrackingService → new `phosphor-wts`**
   - 3,033 lines
   - Depends on: engine-api, zones, snap-engine, screens, phosphor-vdm, window-registry
   - After phases 1-2, all its deps are in libraries
   - This is the critical piece: any compositor gets full window-zone tracking

### Phase 4 — Overlay/rendering consolidation

7. **Merge ZoneShaderItem + VulkanSupport → `phosphor-rendering`**
8. **Extract OverlayService → new `phosphor-overlay`** (optional, large)

---

## Dependency Graph (target state after extraction)

```
phosphor-geometry          (pure math, zero deps)
    └─► phosphor-engine-api    (contracts + base + ScreenModeRouter + WindowRegistry)
            ├─► phosphor-zones         (zone/layout data)
            ├─► phosphor-tiles         (tiling algorithms)
            ├─► phosphor-vdm           (virtual desktop management)
            ├─► phosphor-snap-engine   (snap placement)
            ├─► phosphor-tile-engine   (autotile placement)
            └─► phosphor-wts           (window tracking service)
                    └─► [uses zones, snap-engine, screens, vdm]

phosphor-wayland           (wayland protocols)
    └─► phosphor-layer         (shell surfaces)
            └─► phosphor-surfaces  (surface manager)

phosphor-rendering         (GPU shaders, ShaderNodeRhi)
phosphor-animation         (QML surface animation)
phosphor-layout-api        (layout sources + factory + worker)
phosphor-shortcuts         (shortcut registry)
phosphor-config            (config management)
phosphor-screens           (screen topology)
phosphor-protocol          (wire types)
phosphor-identity          (EDID identity)
phosphor-fsloader          (filesystem loading)
phosphor-audio             (audio feedback)
phosphor-shaders           (shader assets)
```

---

## What a Standalone Compositor/Shell Needs

After full extraction, building a compositor shell binary requires:

```cmake
target_link_libraries(my-compositor
    PhosphorSnapEngine::PhosphorSnapEngine
    PhosphorTileEngine::PhosphorTileEngine
    PhosphorWts::PhosphorWts              # window tracking
    PhosphorVdm::PhosphorVdm              # virtual desktops
    PhosphorLayer::PhosphorLayer          # shell surfaces
    PhosphorSurfaces::PhosphorSurfaces    # surface orchestration
    PhosphorRendering::PhosphorRendering  # GPU rendering
    PhosphorAnimation::PhosphorAnimation  # transitions
    PhosphorLayoutApi::PhosphorLayoutApi  # layout management
    PhosphorShortcuts::PhosphorShortcuts  # keybindings
)
```

No daemon code needed. The shell binary would only need:
- Its own `main.cpp` + startup wiring (~200 lines)
- Its own D-Bus adaptors (or a different IPC mechanism)
- Its own settings backend
