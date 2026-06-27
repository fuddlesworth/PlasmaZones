<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-zones

> Zone data model, layout persistence, zone-detection, and the
> `ILayoutSource` adapter that publishes manual zone layouts to the
> daemon.

## Responsibility

A **zone** is a named rectangular region of screen space with metadata:
appearance, label, and quick-select slot. A **layout** is a collection of
zones plus per-context (screen / desktop / activity) assignment rules.
This library owns:

- **Data model:** `Zone`, `Layout`, and relative/absolute coordinate
  conversion utilities.
- **Detection:** given a cursor point plus modifier state, which zone is
  the window snapping into? `ZoneDetector` implements the `IZoneDetector`
  interface.
- **Layout registry:** `LayoutRegistry` is the concrete catalogue of
  manual zone layouts plus the per-context assignment store (which layout
  is active on which screen / desktop / activity). It implements both
  `IZoneLayoutRegistry` (the read-and-mutate-the-catalogue contract) and
  `ILayoutSourceRegistry` (the provider contract that
  `ZonesLayoutSource` subscribes to).
- **Layout source factory:** `ZonesLayoutSource` adapts a layout into the
  generic `ILayoutSource` contract so the rest of the stack consumes
  manual layouts the same way it consumes autotile output.
  `ZonesLayoutSourceFactory` registers this source with
  `LayoutSourceProviderRegistry`.
- **Highlighter:** `ZoneHighlighter` drives the overlay's per-zone
  emphasis state machine (hover, active drag target, just-snapped flash).
- **Assignment value type:** `AssignmentEntry` and `LayoutAssignmentKey`
  carry the `(screenId, desktop, activity)` keys that `LayoutRegistry`
  stores assignments under. The single source of truth for every
  per-context assignment is the injected
  `PhosphorRule::RuleStore`: each assignment is a context-only
  rule and the cascade is the evaluator's descending-priority walk.

`LayoutRegistry` is the *concrete* counterpart to `IZoneLayoutRegistry`.
The interface exists so consumers that only enumerate or look up layouts
(read-only previews, settings UI thumbnails) depend on the smaller
contract, which is also trivial to mock.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorZones::Zone`                       | Value type: rect, id, label, colors, appearance |
| `PhosphorZones::Layout`                     | Collection of zones plus app-rule auto-snap mappings |
| `PhosphorZones::IZoneDetector`              | Abstract cursor-to-zone resolver |
| `PhosphorZones::ZoneDetector`               | Concrete impl with adjacency-graph navigation |
| `PhosphorZones::IZoneLayoutRegistry`        | Catalogue contract: enumerate, mutate, set active layout |
| `PhosphorZones::LayoutRegistry`             | Concrete `IZoneLayoutRegistry` + per-context assignment store |
| `PhosphorZones::ZonesLayoutSource`          | `ILayoutSource` adapter for manual layouts |
| `PhosphorZones::ZonesLayoutSourceFactory`   | Provider-side factory; registers via `LayoutSourceProviderRegistry` |
| `PhosphorZones::AssignmentEntry`            | Per-context layout assignment value type |
| `PhosphorZones::LayoutAssignmentKey`        | `(screenId, desktop, activity)` key for assignment lookups |
| `PhosphorZones::ZoneHighlighter`            | Overlay highlight state machine |

## Typical use

```cpp
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/ZoneDetector.h>

using namespace PhosphorZones;

LayoutRegistry *registry = /* injected */;
Layout *current = registry->layoutForScreen(screenId, desktop, activity);

// Detect which zone the cursor is over
ZoneDetector det;
det.setLayout(current);
Zone *z = det.zoneAtPoint(cursorPos);
if (z) {
    overlay.highlight(*z);
}
```

## Design notes

- **Zone IDs are UUIDs, never indices.** Reordering zones in the editor
  never orphans a persisted window-to-zone assignment.
- **Relative coordinates on disk.** Zone rects in JSON are normalised to
  the `0.0 - 1.0` range, so the same layout works on any screen size.
  Conversion to pixels happens at read-time.
- **CRUD lives on the concrete registry, not a separate manager.**
  `LayoutRegistry` is the single concrete class. `IZoneLayoutRegistry`
  exists only where mocking is useful. This mirrors
  `phosphor-tiles`'s `AlgorithmRegistry` shape.
- **No process-global singleton.** Composition roots construct one
  `LayoutRegistry` per process and inject it into every consumer. Tests
  build their own per-fixture instance.
- **Quick-layout slots and screen-assignment CRUD live on the consumer.**
  The Phosphor daemon owns the user-facing surface (D-Bus methods,
  quick-layout 1-9, etc.). This library exposes the data the daemon
  drives that surface from.

## Dependencies

- `QtCore`, `QtGui`
- [`phosphor-layout-api`](../phosphor-layout-api/README.md) — `ILayoutSource` and registry contracts
- [`phosphor-geometry`](../phosphor-geometry/README.md) — zone clamping, overlap resolution, rect-to-JSON helpers
- [`phosphor-rule`](../phosphor-rule/README.md) — `RuleStore` + `RuleEvaluator`, the assignment authority (the `LayoutRegistry` ctor takes a `RuleStore*`)
- [`phosphor-identity`](../phosphor-identity/README.md) — window IDs for assignments (private link)
- [`phosphor-screens`](../phosphor-screens/README.md) — screen-id normalisation (private link)

## See also

- [`phosphor-snap-engine`](../phosphor-snap-engine/README.md) — consumes `LayoutRegistry` + `ZoneDetector` to implement manual snapping.
- [`phosphor-tiles`](../phosphor-tiles/README.md) — autotile algorithms produce the same `Layout` shape via `AutotileLayoutSource`.
