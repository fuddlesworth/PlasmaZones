<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-overlay

> Helper library for building overlays with PhosphorLayer. Per-screen
> layer-shell shell hosts, per-slot animator coordination, screen
> hot-plug + rekey plumbing. The mechanism that any Phosphor shell
> needs to compose multi-slot overlays without re-implementing the
> shell-host bookkeeping.

## Responsibility

The overlay subsystem in PlasmaZones today (`src/daemon/overlayservice/`,
~5,900 lines across 14 TUs) is almost entirely domain-agnostic
mechanism:

- per-screen layer-shell shell hosts that group multiple slot-Items
  onto a single `wl_surface`,
- show / hide animator coordination for those slots,
- per-role animator config registration and longest-prefix lookup,
- screen hot-plug, identifier drift, and rekey plumbing,
- shader warming / preview surfaces,
- D-Bus driven slot lifecycle (visible / hidden / dismiss).

The PZ-specific bits (zones, snap-assist, layout-picker, OSDs) are
*content* riding on this mechanism. `phosphor-overlay` lifts the
mechanism into a reusable library so any Phosphor shell (PZ today,
Phosphor-as-standalone tomorrow, third-party plugins per the
plugin-based-compositor direction) can build overlays without
re-implementing the shell-host / slot / animator coordination.

## Status

**Phase 1 scaffolding.** The library currently exposes a stub
`ShellHost` with a default ctor/dtor and a smoke test that links the
library and exercises the construct / destruct path. The real
mechanism (passive-shell creation, screen hot-plug handling, scope-gen
rekey, slot lifecycle) lands in Phase 2 when
`OverlayService::ensurePassiveShellFor` and friends move in.

The full migration plan lives at
[`docs/phosphor-overlay-extraction-plan.md`](../../docs/phosphor-overlay-extraction-plan.md).

## Scope

What lives here:

| Concept | Owner |
|---|---|
| Per-screen layer-shell shell host (one wl_surface, many slots) | this library |
| Named slot (a `QQuickItem` inside the shell, animator-driven show/hide) | this library |
| Per-slot animator config registration (longest-prefix scope lookup) | this library |
| Slot show / hide controller + completion callbacks | this library |
| Screen hot-plug, identifier-drift, rekey plumbing for overlays | this library |

What stays with the consumer (PZ daemon today):

| Concept | Owner |
|---|---|
| Vulkan keep-alive surface | `phosphor-surfaces` |
| Layer-shell role / anchors / keyboard | `phosphor-layer` |
| UI patterns (Hud, Modal, etc.) | `phosphor-shell-patterns` |
| Application-axis roles (e.g. PZ `PzRoles::*`) | consumer |
| Overlay content (zones, snap-assist, layout picker, OSDs) | consumer |

The library knows about *surfaces and slots*. It does not know about
zones, layouts, or audio spectrums. Consumers keep the content
vocabulary.

## Dependencies

- Qt6::Core, Qt6::Quick
- [`phosphor-layer`](../phosphor-layer/README.md). wlr-layer-shell wire
  primitives.
- [`phosphor-shell-patterns`](../phosphor-shell-patterns/README.md).
  UI-pattern recipes (axis 2) the library composes.
- [`phosphor-surfaces`](../phosphor-surfaces/README.md). Managed
  surface lifecycle + scope-generation counter.
- [`phosphor-animation`](../phosphor-animation/README.md). The
  `SurfaceAnimator` that drives every overlay show / hide.
- [`phosphor-screens`](../phosphor-screens/README.md). Screen
  topology + stable identifiers.

## See also

- [`docs/phosphor-overlay-extraction-plan.md`](../../docs/phosphor-overlay-extraction-plan.md).
  Migration phases and design decisions.
- [`phosphor-shell-patterns`](../phosphor-shell-patterns/README.md).
  The companion library for axis-2 UI patterns.
