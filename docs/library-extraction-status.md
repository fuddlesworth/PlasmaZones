# Library Extraction Status

Current state of extracting PlasmaZones into reusable LGPL shared libraries
for compositor/WM/shell independence.

## Extracted Libraries (23)

All live under `libs/`, are SHARED libraries with CMake install + config packages.

### Foundation Layer

| Library | Purpose | Deps |
|---------|---------|------|
| `phosphor-geometry` | Pure math — rect constraint solving (`enforceMinSizes`, `removeRectOverlaps`), coordinate transforms, JSON serialization | Qt6::Core |
| `phosphor-identity` | Window/screen identity (EDID, connector naming, WindowId parsing) | Qt6::Core |
| `phosphor-protocol` | Wire types for IPC (WindowStateEntry, WindowGeometryList, EmptyZoneList, etc.) | Qt6::Core |
| `phosphor-fsloader` | Filesystem layout/config loading | Qt6::Core |

### Engine Layer

| Library | Purpose | Deps |
|---------|---------|------|
| `phosphor-engine` | Engine framework: IPlacementEngine, PlacementEngineBase, WindowRegistry, IWindowTrackingService, IVirtualDesktopManager, IWindowRegistry, GeometryUtils, NavigationContext, EngineTypes | Qt6::Core/Gui, phosphor-geometry, phosphor-identity |
| `phosphor-zones` | Zone/layout data + services: Layout, Zone, IZoneDetector, LayoutRegistry, LayoutFactory, LayoutWorker, LayoutComputeService, GeometryUtils (zone-aware) | Qt6::Core/Gui, phosphor-engine, phosphor-layout-api, phosphor-config, phosphor-screens |
| `phosphor-tiles` | Tiling algorithms (tree-based layout, scripted algorithms) | Qt6::Core/Qml, phosphor-engine, phosphor-layout-api |
| `phosphor-snap-engine` | SnapEngine — zone-based snap placement, SnapState | Qt6::Core/Gui, phosphor-engine, phosphor-zones, phosphor-protocol, phosphor-screens, phosphor-identity |
| `phosphor-tile-engine` | AutotileEngine — autotile placement with config/nav/overflow | Qt6::Core, phosphor-engine, phosphor-tiles, phosphor-screens, phosphor-identity, phosphor-zones |
| `phosphor-workspaces` | Virtual desktop management (KWin D-Bus) | Qt6::Core/DBus, phosphor-engine |
| `phosphor-placement` | Window placement state tracking: zone assignments, floating, auto-snap, resnap, rotation, empty-zone queries | Qt6::Core/Gui, phosphor-engine, phosphor-snap-engine, phosphor-zones, phosphor-protocol, phosphor-screens, phosphor-workspaces |
| `phosphor-layout-api` | Layout source abstraction: ILayoutSource, CompositeLayoutSource, LayoutPreview, AlgorithmMetadata, EdgeGaps | Qt6::Core |
| `phosphor-screens` | Screen manager, virtual screen swapping, screen topology | Qt6::Core/Gui |

### Compositor/Shell Layer

| Library | Purpose | Deps |
|---------|---------|------|
| `phosphor-wayland` | Wayland protocol wrappers: LayerSurface, ToplevelDrag, IdleNotifier, SinglePixelBuffer | Qt6::Core/Gui, wayland-client |
| `phosphor-layer` | Shell surface management: Surface, SurfaceFactory, TopologyCoordinator, Role, ISurfaceAnimator | Qt6::Core/Gui/Quick |
| `phosphor-surfaces` | Higher-level SurfaceManager orchestration | Qt6::Core, phosphor-layer |
| `phosphor-animation` | Surface animation (QML shaders, transitions) | Qt6::Core/Quick |
| `phosphor-rendering` | GPU rendering: ShaderCompiler, ShaderEffect, ShaderNodeRhi | Qt6::Core/Gui |
| `phosphor-shaders` | Shader pack assets (26 packs with previews) | — |

### App/Config Layer

| Library | Purpose | Deps |
|---------|---------|------|
| `phosphor-config` | Config file management | Qt6::Core |
| `phosphor-shortcuts` | Shortcut registry and factory | Qt6::Core |
| `phosphor-audio` | Audio feedback | Qt6::Core |

---

## Remaining in Daemon Binary

### `src/core/` — what's left

| Component | Lines | Notes |
|-----------|-------|-------|
| GeometryUtils (daemon-side) | ~800 | Gap resolution wrappers (`getZoneGeometryForScreen`, `buildEmptyZoneList`) that bridge ISettings to library-level functions. Stays — depends on ISettings. |
| ScreenModeRouter | 163 | Routes screen→engine. Stays — circular dep prevents moving to phosphor-engine. |
| UnifiedLayoutList | ~300 | Stitches zones + tiles + settings for the layout picker. Stays — heavy app-layer coupling. |
| ActivityManager | ~200 | KActivities D-Bus tracking. Could join phosphor-workspaces in a future PR. |
| DaemonGeometryResolver | ~30 | Bridges ISettings → IGeometryResolver for phosphor-placement. |
| Settings / Config layer | ~3,000 | ISettings, Settings, migrations, schema, config stores. Stays — daemon-specific. |
| Platform / Logging / Types / Enums | ~1,500 | Infrastructure. Stays. |

### `src/daemon/` — the shell

| Component | Lines | Notes |
|-----------|-------|-------|
| OverlayService | 7,376 | Possible future `phosphor-overlay` extraction. |
| Daemon wiring | ~5,000 | This IS the app. |
| D-Bus adaptors | ~24,000 | Thin facades over library services. |
| Rendering (ZoneShaderItem) | ~2,000 | Could merge into phosphor-rendering. |
| ShortcutManager | ~500 | Could merge into phosphor-shortcuts. |

---

## Follow-up Tasks

| Task | Effort | Value |
|------|--------|-------|
| Shim cleanup — remove forwarding headers for LayoutFactory, LayoutComputeService, LayoutWorker, WindowRegistry; use library headers directly | Small | Reduces indirection |
| ActivityManager → `phosphor-workspaces` | Small | Completes workspace layer |
| OverlayService → `phosphor-overlay` | Large | Needed for standalone shell without daemon overlays |
| ZoneShaderItem → `phosphor-rendering` | Medium | Consolidates rendering |

---

## Dependency Graph (current state)

```
phosphor-geometry              (pure math, zero deps)
    └─► phosphor-engine            (engine framework + WindowRegistry)
            ├─► phosphor-zones         (zone/layout data + LayoutFactory + LayoutComputeService)
            ├─► phosphor-tiles         (tiling algorithms)
            ├─► phosphor-workspaces    (virtual desktop management)
            ├─► phosphor-snap-engine   (snap placement + SnapState)
            ├─► phosphor-tile-engine   (autotile placement)
            └─► phosphor-placement     (window placement state tracking)
                    └─► [uses zones, snap-engine, screens, workspaces]

phosphor-wayland               (wayland protocols)
    └─► phosphor-layer             (shell surfaces)
            └─► phosphor-surfaces  (surface manager)

phosphor-rendering             (GPU shaders)
phosphor-animation             (QML surface animation)
phosphor-layout-api            (layout sources, EdgeGaps)
phosphor-shortcuts             (shortcut registry)
phosphor-config                (config management)
phosphor-screens               (screen topology)
phosphor-protocol              (wire types)
phosphor-identity              (window/screen identity)
phosphor-fsloader              (filesystem loading)
phosphor-audio                 (audio feedback)
phosphor-shaders               (shader assets)
```

---

## What a Standalone Compositor/Shell Needs

```cmake
target_link_libraries(my-compositor
    PhosphorPlacement::PhosphorPlacement     # window-zone tracking (pulls in engine, zones, snap-engine)
    PhosphorTileEngine::PhosphorTileEngine   # autotile placement
    PhosphorWorkspaces::PhosphorWorkspaces   # virtual desktops
    PhosphorLayer::PhosphorLayer             # shell surfaces
    PhosphorSurfaces::PhosphorSurfaces       # surface orchestration
    PhosphorRendering::PhosphorRendering     # GPU rendering
    PhosphorAnimation::PhosphorAnimation     # transitions
    PhosphorLayoutApi::PhosphorLayoutApi     # layout management
    PhosphorShortcuts::PhosphorShortcuts     # keybindings
)
```

The shell binary provides:
- An `IGeometryResolver` implementation (gap/padding resolution from its config)
- Startup wiring (~200 lines)
- Its own IPC layer (D-Bus, Wayland protocols, or direct)
- Its own settings backend
