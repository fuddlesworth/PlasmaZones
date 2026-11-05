<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-snap-engine

> Manual zone-based placement engine. Implements `IPlacementEngine` for
> screens running a user-drawn layout. Handles auto-snap on window open,
> directional zone navigation, floating, rotation, resnap-on-layout-
> change, and serialisation through `SnapState`.

## Responsibility

The Phosphor daemon dispatches every window-lifecycle and shortcut
event through `PhosphorEngine::IPlacementEngine` so it doesn't have
to branch on the current placement mode. `phosphor-snap-engine` is the
manual-mode implementation: when a screen has a zone layout active,
this engine owns the decisions about which zone a new window joins,
how Super+arrow navigates between zones, how rotation cycles windows,
and what happens on float / unfloat.

The engine reads from shared services (none of which it owns):

- `PhosphorEngine::IWindowTrackingService` — the cross-engine
  shared store of zone assignments, pre-tile geometries, and floating
  state.
- `PhosphorEngine::IVirtualDesktopManager` — current virtual
  desktop.
- `PhosphorZones::LayoutRegistry` + `PhosphorZones::IZoneDetector` —
  layout catalogue and cursor-to-zone resolver from
  [`phosphor-zones`](../phosphor-zones/README.md).
- `PhosphorEngine::ISnapSettings` — exclusions, sticky-window
  handling, "auto-assign all layouts" master toggle (declared inside
  `PhosphorEngine` even though the implementation ships from this
  library).
- `INavigationStateProvider` — narrow read-only contract the daemon's
  `WindowTrackingAdaptor` implements so the engine doesn't reach
  into compositor-shadow state through `QObject*` + invokeMethod.
- `IZoneAdjacencyResolver` — narrow contract for directional zone
  lookups; same shape as the navigation-state provider.

The engine **owns** `SnapState` (per-screen mutable state implementing
`IPlacementState`) and `SnapNavigationTargetResolver` (pure compute for
keyboard-navigation target geometries).

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorSnapEngine::SnapEngine`                   | Concrete `IPlacementEngine` for manual zone layouts |
| `PhosphorSnapEngine::SnapState`                    | Per-screen `IPlacementState`: zone assignments, pre-tile geometry, floating set |
| `PhosphorEngine::ISnapSettings`                    | Settings the engine reads (exclusions, sticky-window handling, master toggle) |
| `PhosphorSnapEngine::INavigationStateProvider`     | Narrow read-only state contract the daemon implements |
| `PhosphorSnapEngine::IZoneAdjacencyResolver`       | Directional zone lookup contract the daemon implements |
| `PhosphorSnapEngine::SnapNavigationTargetResolver` | Pure compute for move / focus / swap / push / cycle / restore target geometries |

## Typical use

Wire up the engine inside the daemon's composition root:

```cpp
#include <PhosphorSnapEngine/SnapEngine.h>

using namespace PhosphorSnapEngine;

auto* snap = new SnapEngine(layoutRegistry,
                            windowTrackingService,
                            zoneDetector,
                            virtualDesktopManager,
                            /*parent*/ daemon);
snap->setEngineSettings(snapSettingsAdaptor);
snap->setNavigationStateProvider(windowTrackingAdaptor);
snap->setZoneAdjacencyResolver(zoneDetectionAdaptor);

// Dispatch from the daemon:
placementEngineRouter.bind("snap", snap);
```

## Design notes

- **Inherits `PlacementEngineBase`.** The base provides the shared
  plumbing (settings injection, stale-window pruning, common signals).
  This engine only adds the manual-zone-specific decisions (which zone,
  when to auto-snap, how to navigate).
- **Typed interfaces, not `QObject*` + invokeMethod.** Daemon dispatch
  uses the narrow `INavigationStateProvider` and `IZoneAdjacencyResolver`
  contracts. They are strictly typed, with no string method names and no opaque
  pointers.
- **State is shared, not owned.** Window-tracking, virtual-desktop,
  layout-registry, and zone-detector all live outside the engine.
  `SnapState` is the one thing this lib owns mutably, and everything else
  is read or commanded.
- **Cross-engine handoff.** `engineId()`, `handoffReceive`, and
  `handoffRelease` implement the placement-engine handoff protocol
  so a window that moves from a snap screen to an autotile screen
  cleanly transitions ownership without the daemon doing the
  bookkeeping.
- **Auto-assign master toggle.** `autoAssignAllLayouts()` is the
  process-wide override, and effective behaviour is
  `globalAutoAssign OR layout->autoAssign()`. Autotile screens are
  short-circuited upstream in `windowOpened` and never reach this
  path.

## Dependencies

- `QtCore`, `QtGui`
- [`phosphor-engine`](../phosphor-engine/README.md) — `IPlacementEngine`, `IPlacementState`, `PlacementEngineBase`, `IWindowTrackingService`, `IVirtualDesktopManager`
- [`phosphor-zones`](../phosphor-zones/README.md) — `LayoutRegistry`, `IZoneDetector`
- [`phosphor-protocol`](../phosphor-protocol/README.md) — navigation result types (`MoveTargetResult`, `FocusTargetResult`, …) via the QtCore-only `PhosphorProtocol::Types` target, and this library never links QtDBus
- [`phosphor-rules`](../phosphor-rules/README.md) — `RuleEvaluator`, `RuleSet` (Exclude-rule evaluation)
- [`phosphor-screens`](../phosphor-screens/README.md) — screen topology (private link)
- [`phosphor-identity`](../phosphor-identity/README.md) — window IDs (private link)

## See also

- [`phosphor-tile-engine`](../phosphor-tile-engine/README.md) — sibling autotile engine. Both implement the same `IPlacementEngine` contract.
