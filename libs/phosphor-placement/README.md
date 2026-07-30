<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-placement

> Window placement state tracking: zone assignments, floating state,
> auto-snap, resnap, rotation, and empty-zone queries.

## Responsibility

Window-zone management state. Tracks which windows are in which zones,
handles floating / unfloating, auto-snaps new windows to their last-used
zone, resnaps when layouts or screens change, rotates windows between
zones, and provides empty-zone queries for snap-assist.

The daemon owns a `WindowTrackingService` instance and exposes it over
D-Bus. Compositor plugins consume the resulting geometry through that
surface rather than linking this library directly.

## Key types

| Type | Purpose |
|------|---------|
| `WindowTrackingService` | Main service — zone assignments, floating, auto-snap, resnap, rotation |
| `IGeometryResolver` | Interface for gap/padding resolution (consumer provides) |
| `PlacementConfig` | Value struct for runtime config (replaces ISettings dependency) |
| `SnapStateResolver` | Injectable seam mapping a window or screen to its owning `SnapState` |

Snap state is per (screen, desktop, activity), so production wires a
`SnapStateResolver` through `setSnapStateResolver`. The `setSnapState`
overload in the example below is the single-store convenience shim, which
builds a resolver whose every arm routes to one store. It suits unit tests
and single-store consumers.

Float state is owned per engine, not by this service. A window floated in
snapping mode is not floated for the autotile or scrolling engine, so never
read one engine's float verdict to gate another's placement.

## Typical use

```cpp
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorPlacement/IGeometryResolver.h>
#include <PhosphorPlacement/PlacementConfig.h>

class MyResolver : public PhosphorPlacement::IGeometryResolver {
    int resolveInnerGap(PhosphorZones::Layout* layout, const QString& screenId) const override {
        return 8;
    }
    PhosphorLayout::EdgeGaps resolveOuterGaps(PhosphorZones::Layout* layout, const QString& screenId) const override {
        return PhosphorLayout::EdgeGaps::uniform(8);
    }
    int defaultBorderWidth() const override { return 2; }
    int defaultBorderRadius() const override { return 0; }
};

MyResolver resolver;
PhosphorPlacement::WindowTrackingService wts(
    layoutManager, screenManager, vdm, &resolver);
wts.setSnapState(snapState);
wts.setWindowRegistry(registry);
```

## Dependencies

- `QtCore`, `QtGui`
- [`phosphor-engine`](../phosphor-engine/README.md), [`phosphor-snap-engine`](../phosphor-snap-engine/README.md), [`phosphor-zones`](../phosphor-zones/README.md), [`phosphor-protocol`](../phosphor-protocol/README.md), [`phosphor-screens`](../phosphor-screens/README.md), [`phosphor-workspaces`](../phosphor-workspaces/README.md), [`phosphor-identity`](../phosphor-identity/README.md), [`phosphor-layout-api`](../phosphor-layout-api/README.md)
