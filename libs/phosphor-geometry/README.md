<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-geometry

> Pure-function geometry helpers: zone clamping, overlap resolution,
> per-window minimum-size enforcement, overlay-coordinate projection,
> and the canonical rect-to-JSON encoder.

## Responsibility

Pure-function corrections shared by the snap engine and the autotile
engine: clamp to screen, eliminate overlaps, grow zones to respect
window minimum sizes, project geometry into an overlay window's local
coordinate system.

Input rects in, output rects out, with no Qt objects, no signals, and no
allocation beyond the result vector. Headless geometry tests link the
lib without GUI infrastructure.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorGeometry::availableAreaToOverlayCoordinates` | Project an "available-area" rect into an overlay window's coords |
| `PhosphorGeometry::snapToRect`                        | `QRectF` → `QRect` with consistent rounding |
| `PhosphorGeometry::enforceMinSizes`                   | Grow zones to fit per-window minimum sizes by stealing surplus from neighbours, then resolve overlap |
| `PhosphorGeometry::clampZonesToScreen`                | Position-only clamp that shifts zones so each window's effective rect stays on screen, sizes preserved |
| `PhosphorGeometry::removeRectOverlaps`                | Resolve residual overlap between zones (used after min-size growth) |
| `PhosphorGeometry::rectToJson`                        | Canonical rect-string format for D-Bus + JSON roundtrip |
| `PhosphorGeometry::JsonKeys`                          | Key constants for the rect-JSON encoder |

## Design notes

- **Position-only vs growth.** `clampZonesToScreen` only shifts and
  never resizes. `enforceMinSizes` is the one path allowed to
  grow or shrink. They run in that order: first grow to fit minimums,
  then position-clamp anything still off-screen. For overlap-tolerant
  algorithms (Deck, Stair, Cascade, Monocle, Paper, Spread,
  horizontal-deck) `enforceMinSizes` is skipped, because neighbour
  stealing would destroy intentional overlap, and the position clamp
  is the only safe correction.
- **Vector-tolerant.** `enforceMinSizes` and `clampZonesToScreen`
  both accept a `minSizes` vector that may be shorter than `zones` (or
  empty for `enforceMinSizes`). Missing entries are treated as
  no-minimum, and extras are ignored.
- **Pure functions.** No Qt objects, no signals, no allocation other
  than the result vector. Engines call these directly inside their
  layout-emit hot path.

## Dependencies

- `QtCore`

## See also

- [`phosphor-snap-engine`](../phosphor-snap-engine/README.md) and [`phosphor-tile-engine`](../phosphor-tile-engine/README.md). Both run zone output through these helpers before publishing.
