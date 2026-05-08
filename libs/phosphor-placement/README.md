<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-placement

> Window placement state tracking — zone assignments, floating state,
> auto-snap, resnap, rotation, and empty-zone queries.

## Responsibility

This is the core business logic for window-zone management. It tracks
which windows are in which zones, handles floating/unfloating, auto-snaps
new windows to their last-used zone, resnaps all windows when layouts or
screens change, rotates windows between zones, and provides empty-zone
queries for snap-assist.

The daemon owns a `WindowTrackingService` instance and exposes it over
D-Bus. Any compositor plugin benefits from this logic without linking
this library directly — the daemon does the thinking, the plugin applies
geometry.

## Key types

| Type | Purpose |
|------|---------|
| `WindowTrackingService` | Main service — zone assignments, floating, auto-snap, resnap, rotation |
| `IGeometryResolver` | Interface for gap/padding resolution (consumer provides) |
| `PlacementConfig` | Value struct for runtime config (replaces ISettings dependency) |

## Usage

```cpp
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorPlacement/IGeometryResolver.h>
#include <PhosphorPlacement/PlacementConfig.h>

class MyResolver : public PhosphorPlacement::IGeometryResolver {
    int resolveZonePadding(PhosphorZones::Layout* layout, const QString& screenId) const override {
        return 8; // or read from your config
    }
    PhosphorLayout::EdgeGaps resolveOuterGaps(PhosphorZones::Layout* layout, const QString& screenId) const override {
        return PhosphorLayout::EdgeGaps::uniform(8);
    }
    int defaultBorderWidth() const override { return 2; }
    int defaultBorderRadius() const override { return 0; }
};

MyResolver resolver;
PhosphorPlacement::WindowTrackingService wts(
    layoutManager, zoneDetector, screenManager, vdm, &resolver);
wts.setSnapState(snapState);
wts.setWindowRegistry(registry);
```

## Link dependencies

```cmake
target_link_libraries(my_target PRIVATE
    PhosphorPlacement::PhosphorPlacement
)
```

Transitive deps: PhosphorEngine, PhosphorSnapEngine, PhosphorZones,
PhosphorProtocol, PhosphorScreens, PhosphorWorkspaces, PhosphorIdentity,
PhosphorLayoutApi.
