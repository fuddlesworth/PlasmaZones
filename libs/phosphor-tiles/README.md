<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-tiles

> Tiling algorithms (Luau scripts via the `pluau` standard library), `TilingState`,
> the algorithm registry, and the `ILayoutSource` adapter that publishes
> autotile output to the daemon.

## Responsibility

Zones are static layouts the user draws in an editor. Autotiling is the
*dynamic* counterpart: as windows open and close, a tiling algorithm
recomputes the zone boundaries automatically. This library owns the
**algorithm vocabulary** and the **per-screen tiling state**. The
runtime engine that drives them lives in
[`phosphor-tile-engine`](../phosphor-tile-engine/README.md), and the generic
Luau host (sandbox, watchdog, marshalling) lives in
[`phosphor-scripting`](../phosphor-scripting/README.md).

- **Algorithms.** `TilingAlgorithm` is the base. `LuauTileAlgorithm` is the
  single concrete implementation, a `TilingAlgorithm` that delegates to a
  Luau script. Every layout (binary-split, master-stack, columns, spiral, …)
  ships as a `.luau` script in `data/algorithms/`, and there are no hard-coded
  C++ geometry algorithms.
- **`pluau` standard library.** Scripts are written against `pluau`, a Luau table of
  geometry primitives (a `Rect` with gap-aware splits, plus ~25 layout/helper
  functions). It is compiled into the library as a Qt resource (`pluau.luau`),
  injected as a **frozen** global before each untrusted script runs, and typed
  for editors by `pluau.d.luau`.
- **Sandbox & limits.** These are provided by `phosphor-scripting`'s
  `LuauEngine` (`luaL_sandbox` read-only globals, a per-engine heap cap, and a
  shared `LuauWatchdog` that aborts runaway scripts at a CPU deadline). They are
  not provided by this library.
- **Loader.** `ScriptedAlgorithmLoader` discovers `*.luau` files from
  consumer-chosen system and user data directories, validates each basename,
  creates a `LuauTileAlgorithm`, and registers it. It hot-reloads via
  `QFileSystemWatcher` with debounced refresh.
- **Registry.** `ITileAlgorithmRegistry` is the read-side contract.
  `AlgorithmRegistry` is the concrete catalogue, mirroring the
  [`phosphor-zones`](../phosphor-zones/README.md) `LayoutRegistry` shape.
- **State.** `TilingState` tracks per-screen window order, master count,
  split-tree, and floating set. Implements
  `PhosphorEngine::IPlacementState` so the daemon's persistence and
  D-Bus surface treat it the same as `SnapState`.
- **Layout source.** `AutotileLayoutSource` wraps a registry as an
  `ILayoutSource` so the rest of the stack consumes autotile output the
  same way it consumes manual zones. `AutotileLayoutSourceFactory`
  registers this source through `LayoutSourceProviderRegistry`.
- **Preview.** `AutotilePreviewRender` paints a layout thumbnail directly
  on a `QPainter` for the algorithm-picker UI.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorTiles::TilingAlgorithm`             | Abstract base; `calculateZones(const TilingParams&) -> QVector<QRect>` |
| `PhosphorTiles::LuauTileAlgorithm`           | The concrete `TilingAlgorithm` that delegates to a Luau script |
| `PhosphorTiles::TilingState`                 | Per-screen window order + master count + split tree; implements `IPlacementState` |
| `PhosphorTiles::SplitTree`                   | Binary-split tree node + ratio used by tree-style algorithms |
| `PhosphorTiles::ITileAlgorithmRegistry`      | Read-side contract; subclasses `ILayoutSourceRegistry` |
| `PhosphorTiles::AlgorithmRegistry`           | Concrete catalogue of registered `LuauTileAlgorithm`s |
| `PhosphorTiles::ScriptedAlgorithmLoader`     | Discovers `*.luau` files, validates names, creates + registers `LuauTileAlgorithm`s |
| `PhosphorTiles::AutotileLayoutSource`        | `ILayoutSource` adapter wrapping a registry |
| `PhosphorTiles::AutotileLayoutSourceFactory` | Provider factory, registers with `LayoutSourceProviderRegistry` |
| `PhosphorTiles::AutotilePreviewRender`       | Paint-a-thumbnail helper for the algorithm picker |

The `pluau` standard library (`src/pluau/pluau.luau`) and its type stubs (`src/pluau/pluau.d.luau`)
are the Luau-side API surface. See
[`docs/architecture/luau-algorithm-authoring.md`](../../docs/architecture/luau-algorithm-authoring.md)
for the authoring guide.

## Typical use

Discover and run the bundled + user Luau algorithms:

```cpp
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>

using namespace PhosphorTiles;

AlgorithmRegistry reg;

// Point a loader at an XDG-relative subdirectory and scan. The bundled
// algorithms live in the system data dir; user algorithms override by
// filename from the writable data dir. Each discovered *.luau registers a
// LuauTileAlgorithm against the injected registry.
ScriptedAlgorithmLoader loader(QStringLiteral("plasmazones/algorithms"), &reg);
loader.scanAndRegister();

auto *alg = reg.algorithm(QStringLiteral("master-stack"));
AutotileLayoutSource src(&reg);
// … src is an ILayoutSource the same way ZonesLayoutSource is.
```

A minimal Luau algorithm (e.g. `$XDG_DATA_HOME/<app>/algorithms/vstack.luau`):

```lua
-- Single column, each window gets an equal vertical slice.
local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Vertical Stack",
        id = "vstack",
        description = "Single column, equal vertical slices",
        minimumWindows = 1,
    },

    tile = function(ctx)
        if ctx.windowCount <= 0 then
            return {}
        end
        return ctx.area:rows(ctx.windowCount, ctx.innerGap)
    end,
}
```

## Design notes

- **Library is algorithms + state. Engine and host live elsewhere.** The
  runtime engine that drives tiling (window-open / focus / float events) is
  [`phosphor-tile-engine`](../phosphor-tile-engine/README.md). The generic Luau
  VM host is [`phosphor-scripting`](../phosphor-scripting/README.md). This
  separation keeps a settings-UI preview from linking the engine just to render
  a thumbnail, and keeps the Luau host reusable beyond tiling.
- **The sandbox is defensive.** `LuauEngine` freezes the global table and the
  `pluau` stdlib (`luaL_sandbox`) before any untrusted script runs, leaving no
  `io`, `os.execute`, filesystem, or network access. It bounds both CPU time
  (the watchdog) and heap (the capped allocator).
- **Algorithms consume positions, not window IDs.** They take a
  `TilingParams` carrying window count, usable area, inner gap, per-window
  metadata, screen info, and custom params, and return a list of zone
  rects. The consuming layer maps windows to zones afterward, so the same
  algorithm drives preview rendering without owning any window bindings.
- **Preview render is headless.** `AutotilePreviewRender` paints a layout
  thumbnail a picker can use in `QListView` delegates.

## Dependencies

- `QtCore`
- [`phosphor-scripting`](../phosphor-scripting/README.md) — the embedded Luau host (`LuauEngine`, `LuauWatchdog`)
- [`phosphor-layout-api`](../phosphor-layout-api/README.md) — `ILayoutSource` + factory + registry contracts
- [`phosphor-engine`](../phosphor-engine/README.md) — `IPlacementState` (implemented by `TilingState`)
- [`phosphor-fsloader`](../phosphor-fsloader/README.md) — filesystem-backed registry skeleton for algorithm discovery

## See also

- [`phosphor-tile-engine`](../phosphor-tile-engine/README.md) — runtime engine that drives algorithms in response to window events.
- [`phosphor-scripting`](../phosphor-scripting/README.md) — the generic embedded Luau host.
