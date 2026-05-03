<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-engine-api

> Unified placement-engine surface a daemon dispatches through, plus the
> shared interfaces (window registry, window tracking, virtual desktops,
> geometry settings) every engine consumes.

## Responsibility

Both the manual snap engine ([`phosphor-snap-engine`](../phosphor-snap-engine/README.md))
and the automatic autotile engine ([`phosphor-tile-engine`](../phosphor-tile-engine/README.md))
do the same kinds of things when a user hits a shortcut or a window opens
or closes: move focus, swap windows, assign to a zone, react to a window
opening or closing. Without a shared interface, a daemon has to branch on
the current mode for every such event. `phosphor-engine-api` names each
of those operations as a user *intent* and lets each engine fulfil the
intent in its own terms, so the daemon's hot path is a single polymorphic
call.

The library also owns the **shared service contracts** every engine reads
from — the canonical window-tracking facade, the window-id canonicaliser,
the virtual-desktop poll, and per-screen geometry settings — so engines
don't carry their own copies and the daemon wires them up once.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorEngineApi::IPlacementEngine`       | Intent dispatcher: every snap / move / focus / swap / assign call goes here. |
| `PhosphorEngineApi::IPlacementState`        | Read-only per-screen state contract. Persistence + D-Bus consume it without caring which engine produced it. |
| `PhosphorEngineApi::PlacementEngineBase`    | Base class implementing the universal window-state FSM (Unmanaged / EngineOwned / Floated) so each engine adds only its mode-specific logic. |
| `PhosphorEngineApi::NavigationContext`      | `(windowId, screenId)` target for an intent. May be empty on early-startup shortcuts. |
| `PhosphorEngineApi::IWindowRegistry`        | Window-id canonicaliser + `appId`-from-instance lookup. |
| `PhosphorEngineApi::IWindowTrackingService` | Cross-engine shared store for zone assignments, pre-tile geometries, floating state. |
| `PhosphorEngineApi::IVirtualDesktopManager` | "Which virtual desktop is current?" — minimal interface, one method. |
| `PhosphorEngineApi::IGeometrySettings`      | Per-screen padding / outer-gap / per-side gap settings. |
| `PhosphorEngineApi::TilingStateKey`         | `(screenId, desktop, activity)` composite key for per-context state. |
| `PhosphorEngineApi::PerScreenKeys`          | JSON key constants for per-screen overrides on disk. |
| `PhosphorEngineApi::JsonKeys`               | JSON key constants for state-serialisation roundtrip. |

## Design notes

- **Interface names intents, not steps.** "Move focused window left" has
  different meaning in tile-swap vs zone-snap mode; the interface just
  names the user's request and each engine does what it needs.
- **Idempotent on empty context.** Every method accepts a
  `NavigationContext` whose fields may be empty on very-early-startup
  shortcuts or when no window is focused. Each engine emits navigation
  feedback with a sensible reason code rather than erroring out.
- **Mutation stays engine-specific.** `IPlacementState` is deliberately
  read-only plus serialization. Mutation goes through engine-specific
  APIs like `SnapState::assignWindowToZone` and `TilingState::addWindow`,
  because the semantics diverge.
- **`PlacementEngineBase` owns the universal FSM.** Every engine has the
  same Unmanaged / EngineOwned / Floated lifecycle for a window, so the
  base implements it once. Concrete engines override only the
  mode-specific intents.
- **Settings interfaces live here, not in engine libs.** `ISnapSettings`
  and `IAutotileSettings` are declared inside `PhosphorEngineApi` (even
  though they ship from the engine libraries) so the daemon can hand the
  same settings adaptor to whichever engine is active without engine-
  specific casts.

## Dependencies

- `QtCore`

## See also

- [`phosphor-snap-engine`](../phosphor-snap-engine/README.md) — manual zone snapping; `SnapState` implements `IPlacementState`.
- [`phosphor-tile-engine`](../phosphor-tile-engine/README.md) — autotile; `TilingState` implements `IPlacementState`.
