<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-layout-api

> Layout description interfaces. The `ILayoutSource` seam plus the value
> types (algorithm metadata, edge gaps, aspect-ratio classes, layout IDs,
> previews) that cross library boundaries.

## Responsibility

Both the zones library and the autotiling library produce "a layout for
this screen at this moment" — zones from a user-drawn JSON description,
autotiling from a dynamic algorithm over the current window set.
Downstream consumers (snap engine, overlay, settings-UI preview)
shouldn't care which source a layout came from.

`phosphor-layout-api` is the shared vocabulary both sides agree on:

- **`ILayoutSource`.** The interface every layout producer implements.
  Given a screen and window-set, return a `Layout`.
- **`ILayoutSourceFactory` / `ILayoutSourceRegistry`.** Provider-side
  contracts so a registry (zones, tiles) can hand out layout sources by
  ID without callers knowing the concrete type.
- **`LayoutSourceProviderRegistry` / `LayoutSourceBundle`.** Composition-
  root glue that lets a process advertise "I have these provider kinds"
  once, and consumers ask the registry rather than the individual libs.
- **`LayoutId`.** Stable string identifier that survives rename, because
  consumers such as assignments and quick-layout slots reference layouts
  across restarts.
- **`AlgorithmMetadata`.** What a tiling algorithm declares about itself:
  display name, description, and configurable parameters. A picker UI
  uses this to render entries for autotile algorithms uniformly.
- **`AspectRatioClass`.** The three buckets layouts declare for
  screen-matching: narrow, normal, wide.
- **`EdgeGaps` and `GapKeys`.** Spacing between zones (inner) and between
  zones and the screen edge (outer), with well-known JSON key constants.
- **`CompositeLayoutSource`.** Chains multiple sources (e.g. zones for
  screen 1, autotile for screen 2) and presents them as one logical
  source.
- **`LayoutPreview`.** Lightweight struct plus free function that paints
  a thumbnail of any layout for a picker UI.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorLayoutApi::ILayoutSource`              | Abstract layout producer |
| `PhosphorLayoutApi::ILayoutSourceFactory`       | Builds an `ILayoutSource` for a given layout-ID |
| `PhosphorLayoutApi::ILayoutSourceRegistry`      | Provider-side enumeration; emits `contentsChanged` |
| `PhosphorLayoutApi::LayoutSourceProviderRegistry` | Process-wide "which providers exist" registrar |
| `PhosphorLayoutApi::LayoutSourceBundle`         | Per-process bundle of resolved provider instances |
| `PhosphorLayoutApi::LayoutId`                   | Stable string identifier, value-semantic |
| `PhosphorLayoutApi::AlgorithmMetadata`          | Tiling algorithm self-description |
| `PhosphorLayoutApi::AspectRatioClass`           | Screen aspect bucket (Narrow / Normal / Wide) |
| `PhosphorLayoutApi::EdgeGaps`                   | Inner/outer spacing value type |
| `PhosphorLayoutApi::GapKeys`                    | JSON key constants for roundtrip |
| `PhosphorLayoutApi::CompositeLayoutSource`      | Multiplexes several `ILayoutSource`s |
| `PhosphorLayoutApi::LayoutPreview`              | Paint-a-thumbnail helper for pickers |

## Typical use

Implement a custom layout source:

```cpp
#include <PhosphorLayoutApi/ILayoutSource.h>

class MyLayoutSource : public PhosphorLayoutApi::ILayoutSource {
public:
    PhosphorZones::Layout layoutFor(
        const PhosphorLayoutApi::LayoutQuery &q) const override
    {
        PhosphorZones::Layout layout;
        // Populate zones based on q.screenRect, q.windows, q.activity…
        return layout;
    }

    PhosphorLayoutApi::LayoutId id() const override
    {
        return PhosphorLayoutApi::LayoutId{QStringLiteral("my-source")};
    }
};
```

Chain two sources for screens with different preferences:

```cpp
CompositeLayoutSource composite;
composite.addScreen("output-1", zonesSource);   // user-drawn
composite.addScreen("output-2", autotileSource);

ILayoutSource *combined = &composite;            // consumers don't know the difference
```

## Design notes

- **Zero Qt-GUI deps in the core.** Most of the contract is `QtCore`-only;
  `QRect` brings in `QtGui` but a headless test or a command-line tool
  can still link against it without QML or Quick.
- **Value-typed `LayoutId`.** `QString` would let two APIs disagree on
  case-sensitivity; wrapping enforces a single canonical form.
- **`CompositeLayoutSource`** is how a daemon handles mixed-source setups
  without special-casing per-screen logic in the snap or autotile engines.
- **Provider registry pattern.** Both `phosphor-zones` and `phosphor-tiles`
  implement `ILayoutSourceFactory` and register through
  `LayoutSourceProviderRegistry`, so the daemon's composition root binds
  providers once rather than knowing about them by name.

## Dependencies

- `QtCore`, `QtGui`
- [`phosphor-identity`](../phosphor-identity/README.md) — window IDs in `LayoutQuery`

## See also

- [`phosphor-zones`](../phosphor-zones/README.md)   — primary `ILayoutSource` implementation (`ZonesLayoutSource`)
- [`phosphor-tiles`](../phosphor-tiles/README.md)   — alternative implementation (`AutotileLayoutSource`)
