# phosphor-compositor SDK Plan

D-Bus client SDK for compositor plugins (KWin, river, Hyprland).
The daemon always runs and owns all placement logic. Plugins are thin
geometry-appliers that implement `ICompositorBridge`.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Daemon Process                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │ phosphor-placement + phosphor-engine +           │    │
│  │ phosphor-zones + phosphor-workspaces +           │    │
│  │ phosphor-snap-engine + phosphor-tile-engine      │    │
│  └─────────────────────────────────────────────────┘    │
│            ▲                                            │
│            │ D-Bus (org.plasmazones)                    │
│            ▼                                            │
│  ┌─────────────────────────────────────────────────┐    │
│  │ D-Bus Adaptors (WindowTracking, WindowDrag,      │    │
│  │ CompositorBridge, Layout, Autotile, Snap, etc.)  │    │
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
            ▲
            │ D-Bus IPC
            ▼
┌─────────────────────────────────────────────────────────┐
│  Compositor Plugin (e.g. KWin effect, river module)   │
│  ┌─────────────────────────────────────────────────┐    │
│  │ phosphor-compositor (SDK)                        │    │
│  │  • ICompositorBridge interface                   │    │
│  │  • D-Bus client helpers (async, fire-and-forget) │    │
│  │  • Handler base classes                          │    │
│  │  • Shared types (WindowInfo, triggers, etc.)     │    │
│  └─────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────┐    │
│  │ Per-compositor adapter (~300-500 lines)           │    │
│  │  • KWinCompositorBridge / riverCompositorBridge │    │
│  │  • Native window handle ↔ void* translation      │    │
│  │  • Effect lifecycle + paint pipeline              │    │
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

## What Goes Into `phosphor-compositor`

### From `src/compositor-common/` (extract as-is)

| File | Lines | What it provides |
|------|-------|------------------|
| `compositor_bridge.h` | 173 | `ICompositorBridge` abstract interface (22 methods), `WindowHandle`, `WindowInfo` |
| `autotile_state.h` | 327 | `BorderState`, `AutotileStateHelpers` — per-screen tiling state tracking |
| `floating_cache.h` | 89 | `FloatingCache` — compositor-side float state mirror |
| `snap_assist_filter.h/cpp` | 140 | `SnapAssistFilter` — candidate building via bridge |
| `trigger_parser.h/cpp` | 156 | `TriggerParser` — modifier/button matching from config |
| `debounced_action.h` | 147 | `DebouncedScreenAction` — generic debounce for screen changes |
| `geometry_helpers.h` | 39 | Fractional-scaling-safe rounding utilities |

**Total existing: 1,071 lines — all library-quality, zero changes needed.**

### New additions for the SDK

| Component | Est. Lines | Purpose |
|-----------|-----------|---------|
| `DaemonClient` class | ~200 | D-Bus client that encapsulates `registerBridge`, service watching, reconnection |
| `IDragHandler` interface | ~30 | `beginDrag` / `dragMoved` / `endDrag` / `dragPolicyChanged` callbacks |
| `IGeometryHandler` interface | ~30 | `applyGeometry` / `applyGeometriesBatch` / `raiseWindows` callbacks |
| `ILifecycleHandler` interface | ~20 | `windowOpened` / `windowClosed` / `windowActivated` hooks |
| `HandlerOrchestrator` class | ~300 | Wires DaemonClient signals → handler dispatchers, owns the state machine |
| CMakeLists.txt + install | ~80 | Library target, export headers, cmake config |

**Total new: ~660 lines.**

### Does NOT go into the SDK

| Component | Why it stays per-compositor |
|-----------|---------------------------|
| `KWinCompositorBridge` | KWin's `EffectWindow*` APIs |
| Paint pipeline (`prePaintWindow`, `paintWindow`) | Compositor-specific rendering |
| Animation attachment | KWin uses `WindowPaintData`, river uses transformers |
| Input routing (keyboard grabs) | Different per compositor |
| Thumbnail capture | Uses KWin's `OffscreenQuickScene` |
| EDID screen ID computation | Each compositor exposes output info differently |
| Border rendering | KWin uses `OutlinedBorderItem` |

## D-Bus Contract (already stable)

The daemon exposes these interfaces — the SDK provides typed client helpers for each:

| Interface | Direction | Key operations |
|-----------|-----------|----------------|
| `org.plasmazones.CompositorBridge` | Plugin → Daemon | `registerBridge()`, `windowOpened()`, `windowClosed()`, `windowActivated()` |
| `org.plasmazones.WindowDrag` | Plugin → Daemon | `beginDrag()`, `dragMoved()`, `endDrag()`, `cancelSnap()` |
| `org.plasmazones.WindowTracking` | Both | `floatWindow()`, `unfloatWindow()`, `getSnappedWindows()` |
| `org.plasmazones.Autotile` | Both | `windowFocused()`, `toggleFloat()`, navigation |
| Daemon signals → Plugin | Daemon → Plugin | `applyGeometryRequested`, `applyGeometriesBatch`, `raiseWindowsRequested`, `dragPolicyChanged` |

## Dependency Graph

```cmake
target_link_libraries(PhosphorCompositor
    PUBLIC
        Qt6::Core
        Qt6::DBus
        PhosphorProtocol::PhosphorProtocol    # wire types
        PhosphorAnimation::PhosphorAnimation  # easing/timing for client-side animation
    PRIVATE
        PhosphorIdentity::PhosphorIdentity    # WindowId parsing
)
```

A compositor plugin then links:
```cmake
target_link_libraries(my_compositor_plugin PRIVATE
    PhosphorCompositor::PhosphorCompositor  # the SDK
    MyCompositor::SDK                       # compositor's own plugin SDK
)
```

## Implementation Plan

### PR 1: Extract `compositor-common/` → `libs/phosphor-compositor/`

1. Create `libs/phosphor-compositor/` with CMakeLists.txt
2. Move all 9 files from `src/compositor-common/` into the library
3. Update `src/CMakeLists.txt` to remove compositor-common sources
4. Update `kwin-effect/CMakeLists.txt` to link `PhosphorCompositor::PhosphorCompositor`
5. Namespace: `PhosphorCompositor` (or keep types in their current namespaces since they're already clean)
6. Build + test

**Estimated effort: Small (mechanical move, same pattern as previous extractions)**

### PR 2: Add `DaemonClient` + handler interfaces

1. Add `DaemonClient` — typed D-Bus client wrapping all daemon interactions
2. Add handler interfaces (`IDragHandler`, `IGeometryHandler`, `ILifecycleHandler`)
3. Add `HandlerOrchestrator` — wires client signals to handlers
4. KWin effect adopts the SDK's `DaemonClient` instead of raw QDBusMessage calls
5. Build + test

**Estimated effort: Medium (new code, but guided by existing patterns in kwin-effect/)**

### PR 3: Documentation + river example

1. Write integration guide: "How to write a PlasmaZones compositor plugin"
2. Minimal river module skeleton (proof-of-concept, ~500 lines)
3. Document the `ICompositorBridge` contract with per-method expectations

**Estimated effort: Medium (documentation-heavy)**

## Migration Path for KWin Effect

After PRs 1-2, the KWin effect (`kwin-effect/`) becomes:
- Links `PhosphorCompositor` instead of `plasmazones_compositor_common`
- Uses `DaemonClient` for D-Bus (currently ~400 lines of raw D-Bus spread across files)
- Implements `ICompositorBridge` (already does — `KWinCompositorBridge`)
- Implements handler callbacks (already does — just formalizes the interfaces)
- Owns KWin-specific rendering, animation attachment, input routing

The KWin effect shrinks by ~400 lines (D-Bus boilerplate absorbed by DaemonClient)
and gains compile-time interface contracts instead of stringly-typed D-Bus calls.

## Success Criteria

- [ ] A river module can link `PhosphorCompositor`, implement `ICompositorBridge`, and get full PlasmaZones functionality
- [ ] KWin effect uses the SDK with zero functionality regression
- [ ] Plugin is <500 lines for basic snap/tiling (excluding animation)
- [ ] No daemon changes needed when adding a new compositor
