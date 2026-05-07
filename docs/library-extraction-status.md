# Library Extraction Status

Current state of extracting PlasmaZones into reusable LGPL shared libraries
for compositor/WM/shell independence.

## Extracted Libraries (23, +1 planned)

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
| `phosphor-workspaces` | Virtual desktops (KWin D-Bus) + KDE Activities (optional KActivities) | Qt6::Core/DBus, phosphor-engine, PlasmaActivities (optional) |
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
| ~~ActivityManager~~ | ~~200~~ | ~~Extracted to phosphor-workspaces (PR #409)~~ |
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

### Remaining Extractions

| Task | Effort | Value | Blocked by |
|------|--------|-------|------------|
| ~~Shim cleanup~~ | ~~Small~~ | ~~Done (PR #408)~~ | — |
| ~~ActivityManager → `phosphor-workspaces`~~ | ~~Small~~ | ~~Done (PR #409)~~ | — |
| OverlayService → `phosphor-overlay` | Large (7,400 lines) | Standalone shell gets its own zone overlays | Nothing |
| ZoneShaderItem → `phosphor-rendering` | Medium (2,000 lines) | Consolidates GPU rendering | Nothing |
| ShortcutManager → `phosphor-shortcuts` | Not viable | — | Deeply coupled to daemon Settings + i18n |

### Compositor-Plugin SDK (`phosphor-compositor`)

The single most strategic extraction for the compositor/WM/shell endgame. Enables
KWin, Wayfire, river, Hyprland, and any wlroots compositor to consume PlasmaZones
as a plugin with a thin per-compositor adapter (~5,600 LOC for Wayfire vs KWin's 7,316).

**What goes into the SDK:**

| Component | Source | Lines |
|-----------|--------|-------|
| `ICompositorBridge` interface (23 methods) | `src/compositor-common/compositor_bridge.h` | ~120 |
| D-Bus protocol types + versioned contract | `src/compositor-common/` + `dbus/*.xml` | ~200 |
| Snap-assist candidate builder | `src/compositor-common/snap_assist_filter.{h,cpp}` | ~150 |
| Autotile state helpers (POD) | `src/compositor-common/autotile_state.h` | ~80 |
| Floating cache (window lookup) | `src/compositor-common/floating_cache.h` | ~60 |
| Trigger parser (drag activation) | `src/compositor-common/trigger_parser.{h,cpp}` | ~200 |
| Handler interfaces (navigation, drag, snap-assist, autotile, screen-change) | Extracted from `kwin-effect/` | ~500 |

**What stays per-compositor (each writes their own adapter):**

| Per-compositor adapter | Purpose |
|------------------------|---------|
| `KWinCompositorBridge` | Bridges KWin `EffectWindow` → `ICompositorBridge` |
| Effect lifecycle | KWin `reconfigure()`, `windowAdded()`, paint pipeline |
| Animation attachment | KWin `paintWindow()` + `WindowPaintData` |
| Input routing | Keyboard grab, modifier detection |

**Handler portability (% KWin API in implementation):**

| Handler | KWin-coupled | Portability |
|---------|-------------|-------------|
| snapassisthandler | 0% | Perfect |
| navigationhandler | 0% | Perfect |
| dragtracker | ~3% | Excellent |
| screenchangehandler | ~5% | Good |
| autotilehandler | ~6% | Good |
| windowanimator | ~7% | Good |

**Blockers (all LOW-MEDIUM):**

1. Window-handle lifetime semantics — `ICompositorBridge` uses `void*`; each compositor has different ownership
2. Animation attachment model — KWin's paint pipeline vs Wayfire's transformers
3. Screen ID stability — EDID serial quirks (already mitigated in KWin)

**Estimated effort:** Medium-Large. The interface already exists; the work is formalizing
it as a library, extracting handler logic from `kwin-effect/`, and defining the
per-compositor adapter contract.

### Standalone Shell Binary

Once the SDK exists, a standalone shell binary proves the full stack works without
KWin. Links all phosphor libs + provides:
- Its own `ICompositorBridge` implementation (Wayland protocol-based, no KWin)
- `IGeometryResolver` from its config
- `main.cpp` (~200 lines of wiring)

---

## Dependency Graph (current state)

```
phosphor-geometry              (pure math, zero deps)
    └─► phosphor-engine            (engine framework + WindowRegistry)
            ├─► phosphor-zones         (zone/layout data + LayoutFactory + LayoutComputeService)
            ├─► phosphor-tiles         (tiling algorithms)
            ├─► phosphor-workspaces    (virtual desktops + activities)
            ├─► phosphor-snap-engine   (snap placement + SnapState)
            ├─► phosphor-tile-engine   (autotile placement)
            └─► phosphor-placement     (window placement state tracking)
                    └─► [uses zones, snap-engine, screens, workspaces]

phosphor-compositor [PLANNED]  (compositor-plugin SDK)
    └─► [uses placement, engine, zones, tiles, protocol, animation]
    └─► ICompositorBridge interface + handler logic
    └─► Per-compositor: KWinCompositorBridge, WayfireCompositorBridge, etc.

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

## What a Compositor Plugin Needs (e.g. KWin effect, Wayfire plugin)

```cmake
target_link_libraries(my-compositor-plugin
    PhosphorCompositor::PhosphorCompositor   # SDK: ICompositorBridge + handlers [PLANNED]
    PhosphorPlacement::PhosphorPlacement     # window-zone tracking
    PhosphorTileEngine::PhosphorTileEngine   # autotile placement
    PhosphorWorkspaces::PhosphorWorkspaces   # virtual desktops + activities
    PhosphorAnimation::PhosphorAnimation     # transitions
)
```

The plugin provides:
- An `ICompositorBridge` implementation (bridges compositor window handles to the SDK)
- Effect lifecycle hooks (window added/removed/activated)
- Animation attachment for the compositor's paint pipeline

## What a Standalone Shell Needs (no KWin)

```cmake
target_link_libraries(my-shell
    PhosphorPlacement::PhosphorPlacement     # window-zone tracking (pulls in engine, zones, snap-engine)
    PhosphorTileEngine::PhosphorTileEngine   # autotile placement
    PhosphorWorkspaces::PhosphorWorkspaces   # virtual desktops + activities
    PhosphorLayer::PhosphorLayer             # shell surfaces (layer-shell overlays)
    PhosphorSurfaces::PhosphorSurfaces       # surface orchestration
    PhosphorRendering::PhosphorRendering     # GPU rendering
    PhosphorAnimation::PhosphorAnimation     # transitions
    PhosphorLayoutApi::PhosphorLayoutApi     # layout management
    PhosphorShortcuts::PhosphorShortcuts     # keybindings
)
```

The shell binary provides:
- An `IGeometryResolver` implementation (gap/padding resolution from its config)
- An `ICompositorBridge` implementation (Wayland protocol-based window management)
- Startup wiring (~200 lines)
- Its own settings backend
