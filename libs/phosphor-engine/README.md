<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-engine

> Unified placement-engine surface a daemon dispatches through, plus the
> shared interfaces (window registry, window tracking, virtual desktops,
> geometry settings) every engine consumes.

## Responsibility

Both the manual snap engine ([`phosphor-snap-engine`](../phosphor-snap-engine/README.md))
and the automatic autotile engine ([`phosphor-tile-engine`](../phosphor-tile-engine/README.md))
do the same kinds of things when a user hits a shortcut or a window opens
or closes: move focus, swap windows, assign to a zone, react to a window
opening or closing. Without a shared interface, a daemon has to branch on
the current mode for every such event. `phosphor-engine` names each
of those operations as a user *intent* and lets each engine fulfil the
intent in its own terms, so the daemon's hot path is a single polymorphic
call.

The library also owns the **shared service contracts** every engine reads
from (the canonical window-tracking facade, the window-id canonicaliser,
the virtual-desktop poll, and per-screen geometry settings), so engines
don't carry their own copies and the daemon wires them up once.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorEngine::IPlacementEngine`       | Intent dispatcher where every snap / move / focus / swap / assign call goes. |
| `PhosphorEngine::IPlacementState`        | Read-only per-screen state contract. Persistence + D-Bus consume it without caring which engine produced it. |
| `PhosphorEngine::PlacementEngineBase`    | Base class providing shared engine plumbing (settings injection, stale-window pruning, common signals). Per-window placement state lives in the engines and the unified `WindowPlacementStore`. Each engine adds only its mode-specific logic. |
| `PhosphorEngine::NavigationContext`      | `(windowId, screenId)` target for an intent. May be empty on early-startup shortcuts. |
| `PhosphorEngine::IWindowRegistry`        | Window-id canonicaliser + `appId`-from-instance lookup. |
| `PhosphorEngine::IWindowTrackingService` | Cross-engine shared store for zone assignments, pre-tile geometries, floating state. |
| `PhosphorEngine::IVirtualDesktopManager` | "Which virtual desktop is current?" A minimal interface with one method. |
| `PhosphorEngine::IGeometrySettings`      | Per-screen padding / outer-gap / per-side gap settings. |
| `PhosphorEngine::TilingStateKey`         | `(screenId, desktop, activity)` composite key for per-context state. |
| `PhosphorEngine::PerScreenKeys`          | JSON key constants for per-screen overrides on disk. |
| `PhosphorEngine::JsonKeys`               | JSON key constants for state-serialisation roundtrip. |

## Design notes

- **Interface names intents, not steps.** "Move focused window left" has
  different meaning in tile-swap vs zone-snap mode. The interface just
  names the user's request and each engine does what it needs.
- **Idempotent on empty context.** Every method accepts a
  `NavigationContext` whose fields may be empty on very-early-startup
  shortcuts or when no window is focused. Each engine emits navigation
  feedback with a sensible reason code rather than erroring out.
- **Mutation stays engine-specific.** `IPlacementState` is deliberately
  read-only plus serialization. Mutation goes through engine-specific
  APIs like `SnapState::assignWindowToZone` and `TilingState::addWindow`,
  because the semantics diverge.
- **`PlacementEngineBase` is a thin shared base.** It provides engine-
  settings injection and a default no-op prune hook. Per-window placement
  state lives in the engines' own state objects and the unified
  `WindowPlacementStore`. Concrete engines implement the mode-specific
  intents.
- **Settings interfaces live here, not in engine libs.** `ISnapSettings`
  and `IAutotileSettings` are declared inside `PhosphorEngine` (even
  though they ship from the engine libraries) so the daemon can hand the
  same settings adaptor to whichever engine is active without engine-
  specific casts.

## Dependencies

- `Qt6::Core`
- [`phosphor-geometry`](../phosphor-geometry/README.md) (public)
- `PhosphorProtocol::Types` (public)
- [`phosphor-identity`](../phosphor-identity/README.md) (private)

## See also

- [`phosphor-snap-engine`](../phosphor-snap-engine/README.md) — manual zone snapping. `SnapState` implements `IPlacementState`.
- [`phosphor-tile-engine`](../phosphor-tile-engine/README.md) — autotile. `TilingState` implements `IPlacementState`.
