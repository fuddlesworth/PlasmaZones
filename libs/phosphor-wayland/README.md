<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-wayland

> Wayland layer-shell integration. The custom QPA plugin that mounts
> `QWindow`s as `zwlr_layer_shell_v1` surfaces, plus the
> `LayerSurface` Qt wrapper that exposes anchors, exclusive zone,
> margins, and keyboard interactivity as `Q_PROPERTY`s.

## Responsibility

Qt's standard QPA plugins (`wayland`, `xcb`) create desktop-level
windows. Overlays — zone outlines, snap-assist previews, shader
wallpapers — need to live on a **layer-shell surface** with per-surface
anchor, margin, exclusive-zone, and keyboard-interactivity
configuration that the regular Qt surface API doesn't expose.

`phosphor-wayland` is the lowest level of the stack:

- **A custom QPA plugin** (`phosphorwayland`). Loaded by setting
  `QT_WAYLAND_SHELL_INTEGRATION=phosphorwayland` before
  `QGuiApplication`. Hosts a `QQuickWindow` on top of a
  `wlr-layer-shell-v1` surface.
- **`LayerSurface`** — the Qt wrapper around a layer-shell role.
  Pure Qt API: callers get `QObject` + `Q_PROPERTY`s and never see
  Wayland types directly. The wrapper communicates with the QPA
  plugin via a small set of dynamic property keys
  (`LayerSurfaceProps::*`), so a build that doesn't link this lib
  can still construct a `QWindow` with the same properties and have
  the plugin pick them up.
- **`registerLayerShellPlugin()`** — a header-only helper that sets
  the QPA env var when a Wayland compositor is actually running.
  Respects an existing `QT_WAYLAND_SHELL_INTEGRATION` (so debug
  workflows can override) and falls back to checking
  `XDG_RUNTIME_DIR/wayland-{0,1}` for compositors like COSMIC that
  don't set `WAYLAND_DISPLAY`.

[`phosphor-layer`](../phosphor-layer/README.md) sits one level up and
provides the policy layer (roles, per-screen surface registry,
topology coordinator). Application code typically uses `phosphor-layer`
or [`phosphor-surfaces`](../phosphor-surfaces/README.md) and only
touches this lib through `registerLayerShellPlugin()` at startup.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorWayland::LayerSurface`            | `QObject` wrapper around a `zwlr_layer_shell_v1` surface; `Q_PROPERTY` for layer / anchors / exclusive zone / keyboard / scope / screen / margins |
| `PhosphorWayland::LayerSurface::Layer`     | Background / Bottom / Top / Overlay |
| `PhosphorWayland::LayerSurface::Anchors`   | `Q_FLAGS` of `AnchorTop / Bottom / Left / Right`; `AnchorAll` constant for full-screen anchors |
| `PhosphorWayland::LayerSurface::KeyboardInteractivity` | None / Exclusive / OnDemand |
| `PhosphorWayland::LayerSurfaceProps`       | Property-key constants used by `LayerSurface` ↔ QPA plugin |
| `PhosphorWayland::registerLayerShellPlugin`| Header-only env-var setup before `QGuiApplication` |

## Typical use

Register the plugin before `QGuiApplication`, then build a layer
surface in the usual Qt style:

```cpp
#include <PhosphorWayland/LayerShellPluginLoader.h>
#include <PhosphorWayland/LayerSurface.h>

int main(int argc, char** argv) {
    PhosphorWayland::registerLayerShellPlugin();   // safe no-op on non-Wayland sessions
    QGuiApplication app(argc, argv);

    using namespace PhosphorWayland;
    auto* surface = new LayerSurface();
    surface->setLayer(LayerSurface::LayerOverlay);
    surface->setAnchors(LayerSurface::AnchorAll);
    surface->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityOnDemand);
    surface->setScope(QStringLiteral("zone-overlay"));
    surface->setScreen(QGuiApplication::primaryScreen());
    surface->setMargins(QMargins(8, 8, 8, 8));
    // … attach a QQuickWindow to it via the QPA plugin's property contract
    return app.exec();
}
```

## Design notes

- **Pure Qt API surface.** No Wayland types leak through `LayerSurface`.
  Callers see `QMargins`, `QScreen*`, and Qt enums — the QPA plugin
  consumes the dynamic properties on the underlying `QWindow` and
  drives `wl_layer_shell` itself.
- **Header-only env-var setup.** `registerLayerShellPlugin()` is
  inline so callers don't link the QPA plugin's library just to set
  one environment variable. The plugin itself loads through Qt's
  standard QPA discovery once the env var is set.
- **Compositor-running detection is conservative.**
  `WAYLAND_DISPLAY` proves a compositor is up; we don't use
  `XDG_SESSION_TYPE` because it can be `wayland` in SSH sessions or
  before the compositor starts. The fallback checks
  `$XDG_RUNTIME_DIR/wayland-{0,1}` for COSMIC and socket-activation
  setups that don't set `WAYLAND_DISPLAY`.
- **Compositor-lost detection is incomplete.** The QPA plugin
  receives the `wlr-layer-shell` global-removal signal but doesn't
  yet expose it through a public API. The default
  `PhosphorWaylandTransport` in
  [`phosphor-layer`](../phosphor-layer/README.md) currently fires
  compositor-lost only on `aboutToQuit` (clean exit). Mid-session
  compositor crashes are not detected until this lib gains a public
  global-removal accessor.
- **Split out of `phosphor-shell`.** The Phase B refactor moved the
  shader-domain headers to
  [`phosphor-shaders`](../phosphor-shaders/README.md); what stayed
  here is the Wayland layer-shell integration.

## Dependencies

- `QtCore`, `QtGui`, `QtQuick`
- `wayland-client`, `wlr-layer-shell-v1` protocol, `Qt6::WaylandClientPrivate`

## See also

- [`phosphor-layer`](../phosphor-layer/README.md) — policy layer (roles, per-screen registry, topology coordinator) on top of `LayerSurface`.
- [`phosphor-surfaces`](../phosphor-surfaces/README.md) — higher-level surface manager with QML + Vulkan wiring.
- [`phosphor-shaders`](../phosphor-shaders/README.md) — sibling lib that owns the shader-domain headers split out of the old `phosphor-shell`.
