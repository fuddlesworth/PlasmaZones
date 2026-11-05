<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-layer

> Layer-shell surface primitives. The `Surface` per-overlay wrapper, the
> `SurfaceFactory` that builds them with injected transport / engine /
> screen providers, the per-screen registry, and the topology
> coordinator that reconciles surfaces as monitors come and go.

## Responsibility

[`phosphor-wayland`](../phosphor-wayland/README.md) owns the low-level
`zwlr_layer_shell_v1` plumbing: the QPA plugin and the raw
`LayerSurface`. `phosphor-layer` sits one level up. It's the **policy
layer** that says "we currently have these overlay surfaces, they belong
to these screens, their per-surface animators look like this, and each
has one of these well-known roles."

- **Role primitive** (`Role`): a value struct bundling the
  wlr-layer-shell parameters that are immutable after `show()`: layer,
  anchors, keyboard interactivity, exclusive zone, default margins,
  scope prefix. Pure protocol vocabulary, no UI-pattern semantics. The
  named UI patterns (`Wallpaper()`, `Hud()`, `Modal()`, `Floating()`,
  `Panel(Edge)`, `Toast(Corner)`) live in the sibling
  [`phosphor-shell-patterns`](../phosphor-shell-patterns/README.md)
  library so this lib stays domain-agnostic.
- **Surface** (`Surface`): one layer-shell window. Carries its config,
  knows its current screen, exposes show / hide.
- **SurfaceFactory** (`SurfaceFactory`): stateless builder that turns a
  `SurfaceConfig` + injected dependencies into a live `Surface`. One
  factory per process, and one `Surface` per `create()` call.
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
  `PhosphorWaylandTransport`, and tests use a mock.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorLayer::Surface`                    | Per-surface wrapper around a layer-shell role |
| `PhosphorLayer::SurfaceFactory`             | Stateless builder: `SurfaceConfig` → `Surface` |
| `PhosphorLayer::SurfaceConfig`              | Per-surface descriptor: role, screen, QML source, properties |
| `PhosphorLayer::Role`                       | Value struct for wlr-layer-shell config (layer + anchors + kbd + zone + margins + scope prefix) |
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
SurfaceFactory factory(SurfaceFactory::Deps{.transport = transport.get(),
                                            .screens = screens.get(),
                                            .engineProvider = engineProvider,
                                            .animator = animator});

SurfaceConfig cfg;
// Role is a plain value type. Construct one directly or use a recipe from
// the sibling phosphor-shell-patterns library if you prefer named UI
// patterns (Wallpaper, Hud, Modal, Floating, Panel, Toast).
cfg.role       = Role{Layer::Overlay,
                      AnchorAll,
                      -1,
                      KeyboardInteractivity::None,
                      QMargins(),
                      QStringLiteral("zone-outline")};
cfg.screen     = targetScreen;  // QScreen*; nullptr resolves to the provider's primary()
cfg.contentUrl = QUrl(QStringLiteral("qrc:/overlays/ZoneOutline.qml"));
cfg.contextProperties.insert(QStringLiteral("zones"), QVariant::fromValue(zoneList));

Surface *s = factory.create(std::move(cfg), /*parent*/ this);
s->show();
// Later:
s->hide();
```

## Design notes

- **Role identity is the scope prefix.** Two `Role` values with the
  same scope prefix refer to the same logical surface kind, and the
  per-screen registry uses that to collapse duplicates. Consumers
  compose their roles by taking a pattern from
  `phosphor-shell-patterns` and stamping their own scope prefix on top.
- **Transport is injectable.** Every interaction with the Wayland layer-
  shell goes through `ILayerShellTransport`. Tests verify that the
  factory asked the transport to change anchor to Top+Right without a
  live Wayland compositor.
- **Animator is optional.** Surfaces without an animator show and hide
  with no transition. Attach an `ISurfaceAnimator` to get fade, slide,
  or scale.
- **`defaults/` lives next to the interfaces.** Each `I*` interface ships
  with a bundled implementation under `PhosphorLayer/defaults/` so
  composition roots can wire the common case in three lines and replace
  any single piece for tests or alternative compositors.

## Dependencies

- `QtCore`, `QtGui`, `QtQml`, `QtQuick`
- [`phosphor-wayland`](../phosphor-wayland/README.md). Default transport binds to its `LayerSurface`.

## See also

- [`phosphor-shell-patterns`](../phosphor-shell-patterns/README.md). UI-pattern recipes (Wallpaper, Hud, Modal, Floating, Panel, Toast) built on this library's `Role` primitive.
- [`phosphor-surfaces`](../phosphor-surfaces/README.md). Higher-level surface manager built on these primitives, adds Vulkan and pipeline-cache wiring.
- [`phosphor-wayland`](../phosphor-wayland/README.md). The QPA plugin and raw layer-shell binding the default transport talks to.
