<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-tiles

> Tiling algorithms (built-in C++ + sandboxed JavaScript), `TilingState`,
> the algorithm registry, and the `ILayoutSource` adapter that publishes
> autotile output to the daemon.

## Responsibility

Zones are static layouts the user draws in an editor. Autotiling is the
*dynamic* counterpart: as windows open and close, a tiling algorithm
recomputes the zone boundaries automatically. This library owns the
**algorithm vocabulary** and the **per-screen tiling state**; the
runtime engine that drives them lives in
[`phosphor-tile-engine`](../phosphor-tile-engine/README.md).

- **Algorithms.** `TilingAlgorithm` is the base; concrete C++ algorithms
  (binary-split, master-stack, columns, spiral, …) implement it
  internally. `ScriptedAlgorithm` is a `TilingAlgorithm` that delegates
  to a user-supplied JavaScript function.
- **Sandbox.** `ScriptedAlgorithmSandbox` (a `QJSEngine` subclass) strips
  dangerous globals; `ScriptedAlgorithmJsBuiltins` is the allowlist of
  helpers scripts can call (distribution, split solvers, tree walkers,
  fallbacks). `ScriptedAlgorithmWatchdog` kills runaways that exceed a
  CPU budget.
- **Loader.** `ScriptedAlgorithmLoader` discovers `*.js` files from a
  consumer-chosen data directory, validates the metadata header, and
  loads them into the sandbox.
- **Registry.** `ITileAlgorithmRegistry` is the read-side contract;
  `AlgorithmRegistry` is the concrete catalogue holding both built-ins
  and scripted algorithms, mirroring the
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
| `PhosphorTiles::TilingState`                 | Per-screen window order + master count + split tree; implements `IPlacementState` |
| `PhosphorTiles::SplitTree`                   | Binary-split tree node + ratio used by tree-style algorithms |
| `PhosphorTiles::ITileAlgorithmRegistry`      | Read-side contract; subclasses `ILayoutSourceRegistry` |
| `PhosphorTiles::AlgorithmRegistry`           | Concrete: built-ins + scripted, registers ScriptedAlgorithm |
| `PhosphorTiles::AutotileLayoutSource`        | `ILayoutSource` adapter wrapping a registry |
| `PhosphorTiles::AutotileLayoutSourceFactory` | Provider factory, registers with `LayoutSourceProviderRegistry` |
| `PhosphorTiles::AutotilePreviewRender`       | Paint-a-thumbnail helper for the algorithm picker |
| `PhosphorTiles::ScriptedAlgorithm`           | `TilingAlgorithm` that delegates to a JS function |
| `PhosphorTiles::ScriptedAlgorithmLoader`     | Discovers `*.js` files, validates signatures, loads into sandbox |
| `PhosphorTiles::ScriptedAlgorithmSandbox`    | `QJSEngine` subclass with stripped globals |
| `PhosphorTiles::ScriptedAlgorithmJsBuiltins` | The JS API scripts can call |
| `PhosphorTiles::ScriptedAlgorithmWatchdog`   | CPU-budget killer for runaway scripts |

## Typical use

Register and run a scripted algorithm:

```cpp
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>

using namespace PhosphorTiles;

// Built-in C++ algorithms (master-stack, BSP, columns, …) register
// themselves in the constructor — there is no discovery call.
AlgorithmRegistry reg;

// User *.js algorithms: point a loader at an XDG-relative subdirectory
// and scan. Discovered scripts register against the injected registry.
ScriptedAlgorithmLoader loader(QStringLiteral("plasmazones/algorithms"), &reg);
loader.scanAndRegister();

auto *alg = reg.algorithm(QStringLiteral("master-stack"));
AutotileLayoutSource src(&reg);
// … src is an ILayoutSource the same way ZonesLayoutSource is.
```

A minimal scripted algorithm (e.g. `$XDG_DATA_HOME/<app>/algorithms/vstack.js`):

```js
// Single column, each window gets an equal vertical slice.
var metadata = {
    name: "Vertical Stack",
    id: "vstack",
    description: "Single column, equal vertical slices",
    minimumWindows: 1
};

function calculateZones(params) {
    const { area, innerGap } = params;
    const n = params.windowCount;
    if (n <= 0) return [];
    const heights = distributeWithGaps(area.height, n, innerGap);
    const zones = [];
    let y = area.y;
    for (let i = 0; i < n; i++) {
        zones.push({ x: area.x, y, width: area.width, height: heights[i] });
        y += heights[i] + innerGap;
    }
    return zones;
}
```

## Design notes

- **Library is algorithms + state; engine lives elsewhere.** The runtime
  engine that drives tiling (handles window-open / focus / float events)
  is [`phosphor-tile-engine`](../phosphor-tile-engine/README.md). This
  separation keeps a settings-UI preview from linking the engine just to
  render a thumbnail.
- **JS sandbox is defensive.** No global `Qt`, `console`, `require`,
  `fetch`, or `process`. Only the allowlist in
  `ScriptedAlgorithmJsBuiltins`. The watchdog kills scripts that exceed
  CPU budget.
- **Algorithms consume positions, not window IDs.** They take a
  `TilingParams` carrying window count, usable area, inner gap, per-window
  metadata, screen info, and custom params, and return a list of zone
  rects. The consuming layer maps windows to zones afterward, so the same
  algorithm drives preview rendering without owning any window bindings.
- **Preview render is headless.** `AutotilePreviewRender` paints into a
  `QImage` that a layout picker can use in `QListView` delegates.

## Dependencies

- `QtCore`, `QtQml` (for `QJSEngine` in the sandbox)
- [`phosphor-layout-api`](../phosphor-layout-api/README.md) — `ILayoutSource` + factory + registry contracts
- [`phosphor-engine`](../phosphor-engine/README.md) — `IPlacementState` (implemented by `TilingState`)
- [`phosphor-fsloader`](../phosphor-fsloader/README.md) — filesystem-backed registry skeleton for scripted-algorithm discovery

## See also

- [`phosphor-tile-engine`](../phosphor-tile-engine/README.md) — runtime engine that drives algorithms in response to window events.
