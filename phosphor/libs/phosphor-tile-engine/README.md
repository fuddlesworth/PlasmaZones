<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-tile-engine

> Automatic-tiling placement engine. Implements `IPlacementEngine` for
> screens running an autotile algorithm; handles dynamic re-layout on
> window open / close / focus, navigation, ratio / master-count
> adjustments, per-screen overrides, and overflow auto-floating.

## Responsibility

[`phosphor-tiles`](../phosphor-tiles/README.md) owns the algorithms
themselves (`TilingAlgorithm`, `TilingState`, the Luau algorithm binding).
`phosphor-tile-engine` is the **runtime engine** that drives those
algorithms in response to compositor events. Splitting them lets
settings UI render an algorithm preview without linking the engine,
and lets engine work iterate without touching the algorithm
vocabulary.

The engine manages:

- **Lifecycle.** Window-open kicks the active algorithm. Window-close
  removes the entry from `TilingState` and re-runs layout. Focus
  events drive the cursor / focus model the consumer cares about.
- **Navigation.** `NavigationController` (a stateless helper holding
  a back-pointer to the engine) handles focus next / previous /
  master, swap, rotation, directional focus / swap, position moves,
  and master ratio / count adjustments. Stateless means no member
  data forks from the engine.
- **Per-screen overrides.** `PerScreenConfigResolver` resolves
  effective gap / algorithm / split-ratio / master-count by falling
  back through per-screen overrides → global `AutotileConfig`.
- **Overflow.** `OverflowManager` tracks windows auto-floated when
  the tiled count exceeds `maxWindows`. Returns lists of windows to
  float / unfloat, and the engine performs the mutations and emits the
  signals so `TilingState` stays the single mutation point.
- **Restore.** Autotile keeps no restore queue of its own: a
  closed-while-autotiled window's slot is captured into the shared
  `WindowPlacementStore` (via `capturePlacement`), so reopening the
  same `appId` restores it to the same tiling slot. The store's
  per-`appId` cap of 16 entries prevents unbounded growth.
- **Per-algorithm settings.** `AlgorithmSettings` (split ratio,
  master count, custom params) saves on switch-away and restores on
  switch-back, so each algorithm remembers its tuning.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorTileEngine::AutotileEngine`          | Concrete `IPlacementEngine` for autotile screens |
| `PhosphorTileEngine::AutotileConfig`          | Global config: default algorithm, gaps, master count, per-algorithm `AlgorithmSettings` map |
| `PhosphorEngine::IAutotileSettings`           | Settings contract the engine reads (declared in `PhosphorEngine`, implementation ships from here) |
| `PhosphorTileEngine::NavigationController`    | Stateless helper for focus / swap / rotate / split-ratio / master-count |
| `PhosphorTileEngine::OverflowManager`         | Per-screen tracking of auto-floated windows when `maxWindows` is exceeded |
| `PhosphorTileEngine::PerScreenConfigResolver` | Resolves per-screen overrides → global config |
| `PhosphorTileEngine::AlgorithmSettings`       | Per-algorithm split ratio + master count + custom params (saved on switch-away) |

## Typical use

Wire up the engine inside the daemon's composition root:

```cpp
#include <PhosphorTileEngine/AutotileEngine.h>

using namespace PhosphorTileEngine;

auto* autotile = new AutotileEngine(layoutRegistry,           // from phosphor-zones
                                    windowTrackingService,
                                    screenManager,
                                    algorithmRegistry,        // from phosphor-tiles
                                    /*parent*/ daemon);
autotile->setWindowRegistry(windowRegistry);          // late-bound app-id resolver
autotile->setEngineSettings(autotileSettingsAdaptor); // base-class settings wiring
autotile->refreshConfigFromSettings();                // pull initial config from settings

placementEngineRouter.bind("autotile", autotile);
```

## Design notes

- **Inherits `PlacementEngineBase`.** Like the snap engine, it reuses the
  base's shared plumbing (settings injection, pruning, common signals) and
  adds the autotile-specific intent dispatch.
- **`NavigationController` is stateless.** Every method dispatches back
  to the engine for reads / writes. The controller carries only a
  back-pointer, so it isolates navigation logic without forking any
  member state.
- **`OverflowManager` doesn't mutate `TilingState`.** It returns the
  lists of windows to float / unfloat, and the engine performs the
  mutations and emits signals. This keeps `TilingState` the sole
  mutation point and overflow accounting trivially testable.
- **Per-app restore is bounded.** `WindowPlacementStore::MaxPerApp = 16`
  caps the per-`appId` queue so a misbehaving app can't grow
  restore state without limit.
- **Algorithm settings are saved per-algorithm, not globally.**
  Switching from `master-stack` to `spiral` and back restores the
  prior split ratio and master count for `master-stack`, so users keep
  their tuning per algorithm.

## Dependencies

- `QtCore`
- [`phosphor-engine`](../phosphor-engine/README.md) — `IPlacementEngine`, `PlacementEngineBase`, `IWindowRegistry`, `IWindowTrackingService`, `PerScreenKeys`
- [`phosphor-tiles`](../phosphor-tiles/README.md) — `TilingAlgorithm`, `TilingState`, `ITileAlgorithmRegistry`, `AutotileConstants`
- [`phosphor-zones`](../phosphor-zones/README.md) — `Layout`, `LayoutRegistry`
- [`phosphor-layout-api`](../phosphor-layout-api/README.md) — `EdgeGaps`
- [`phosphor-screens`](../phosphor-screens/README.md) — `PhosphorScreens::ScreenManager`
- [`phosphor-identity`](../phosphor-identity/README.md) — window IDs (private link)

## See also

- [`phosphor-tiles`](../phosphor-tiles/README.md) — algorithm vocabulary, the Luau binding, and `TilingState`.
- [`phosphor-snap-engine`](../phosphor-snap-engine/README.md) — sibling manual-zone engine.
