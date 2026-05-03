<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-layer

> Layer-shell surface primitives. The `Surface` per-overlay wrapper, the
> `SurfaceFactory` that builds them with injected transport / engine /
> screen providers, the per-screen registry, and the topology
> coordinator that reconciles surfaces as monitors come and go.

## Responsibility

[`phosphor-wayland`](../phosphor-wayland/README.md) owns the low-level
`zwlr_layer_shell_v1` plumbing — the QPA plugin and the raw
`LayerSurface`. `phosphor-layer` sits one level up. It's the **policy
layer** that says "we currently have these overlay surfaces, they belong
to these screens, their per-surface animators look like this, and each
has one of these well-known roles."

- **Role vocabulary** (`Role`): named overlay kinds — zone-outline,
  snap-preview, wallpaper-effect, snap-assist, OSD. Each role carries
  its default anchor, margin, and interactivity settings so individual
  consumers don't re-invent them.
- **Surface** (`Surface`): one layer-shell window. Carries its config,
  knows its current screen, exposes show / hide.
- **SurfaceFactory** (`SurfaceFactory`): stateless builder that turns a
  `SurfaceConfig` + injected dependencies into a live `Surface`. One
  factory per process; one `Surface` per `create()` call.
- **Per-screen registry** (`ScreenSurfaceRegistry<T>`): templated bag
  that owns one `Surface` (or subclass) per screen for surfaces with
  AllScreens affinity.
- **Topology coordinator** (`TopologyCoordinator`): debounces
  `screensChanged` bursts and tells the registry / factory to reconcile.
- **Surface store** (`ISurfaceStore`): persists the small slice of
  per-surface state that needs to survive a restart. `JsonSurfaceStore`
  is the bundled file-backed implementation.
- **Screen provider** (`IScreenProvider`): abstract source of
  `(screen-id, geometry, scale, refresh-rate)` tuples. Lets tests inject
  fake screen sets. `DefaultScreenProvider` is the bundled default.
- **Surface animator** (`ISurfaceAnimator`): lets a consumer drive a
  show or hide animation on the layer surface itself (opacity fade,
  slide-from-edge, etc.) without needing to know anything about
  layer-shell internals.
- **Layer-shell transport** (`ILayerShellTransport`): the seam between
  this library and the Wayland binding. Default impl is
  `PhosphorWaylandTransport`; tests use a mock.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorLayer::Surface`                    | Per-surface wrapper around a layer-shell role |
| `PhosphorLayer::SurfaceFactory`             | Stateless builder: `SurfaceConfig` → `Surface` |
| `PhosphorLayer::SurfaceConfig`              | Per-surface descriptor: role, screen, QML source, properties |
| `PhosphorLayer::Role`                       | Enum + metadata for well-known overlay roles |
| `PhosphorLayer::ScreenSurfaceRegistry<T>`   | Per-screen surface tracking (template) |
| `PhosphorLayer::TopologyCoordinator`        | Debounces `screensChanged`; reconciles registry |
| `PhosphorLayer::ISurfaceStore`              | Persistence of per-surface state across restarts |
| `PhosphorLayer::IScreenProvider`            | Enumerates screens with geometry and scale |
| `PhosphorLayer::ISurfaceAnimator`           | Show / hide animator for layer surfaces |
| `PhosphorLayer::ILayerShellTransport`       | Adapter into the Wayland binding |
| `PhosphorLayer::IQmlEngineProvider`         | Shared `QQmlEngine` for surface QML instantiation |
| `PhosphorLayer::JsonSurfaceStore`           | Bundled file-backed `ISurfaceStore` (`QSaveFile`-atomic) |
| `PhosphorLayer::DefaultScreenProvider`      | Bundled `IScreenProvider` over `QGuiApplication::screens()` |
| `PhosphorLayer::NoOpSurfaceAnimator`        | Animator that shows / hides instantly (default) |
| `PhosphorLayer::PhosphorWaylandTransport`   | Default `ILayerShellTransport` over `phosphor-wayland`'s `LayerSurface` |
| `PhosphorLayer::XdgToplevelTransport`       | Fallback transport using `xdg_toplevel` (no layer-shell) |

## Typical use

```cpp
#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/defaults/PhosphorWaylandTransport.h>
#include <PhosphorLayer/defaults/DefaultScreenProvider.h>

using namespace PhosphorLayer;

auto transport = std::make_unique<PhosphorWaylandTransport>();
auto screens   = std::make_unique<DefaultScreenProvider>();
SurfaceFactory factory(transport.get(), engineProvider, screens.get(), animator);

SurfaceConfig cfg;
cfg.role       = Role::ZoneOutline;
cfg.screenId   = "output-1";
cfg.qmlSource  = QUrl(QStringLiteral("qrc:/overlays/ZoneOutline.qml"));
cfg.contextProperties.insert(QStringLiteral("zones"), QVariant::fromValue(zoneList));

Surface *s = factory.create(cfg, /*parent*/ this);
s->show();
// … later
s->hide();
```

## Design notes

- **Role is the primary key.** "Zone-outline overlay on screen 1" has a
  single identity; the per-screen registry collapses duplicates.
- **Transport is injectable.** Every interaction with the Wayland layer-
  shell goes through `ILayerShellTransport`. Tests verify that the
  factory asked the transport to change anchor to Top+Right without a
  live Wayland compositor.
- **Animator is optional.** Surfaces without an animator show and hide
  with no transition; attach an `ISurfaceAnimator` to get fade, slide,
  or scale.
- **`defaults/` lives next to the interfaces.** Each `I*` interface ships
  with a bundled implementation under `PhosphorLayer/defaults/` so
  composition roots can wire the common case in three lines and replace
  any single piece for tests or alternative compositors.

## Dependencies

- `QtCore`, `QtGui`, `QtQml`
- [`phosphor-wayland`](../phosphor-wayland/README.md) — default transport binds to its `LayerSurface`

## See also

- [`phosphor-surfaces`](../phosphor-surfaces/README.md) — higher-level surface manager built on these primitives, adds Vulkan + pipeline-cache wiring.
- [`phosphor-wayland`](../phosphor-wayland/README.md) — the QPA plugin and raw layer-shell binding the default transport talks to.
