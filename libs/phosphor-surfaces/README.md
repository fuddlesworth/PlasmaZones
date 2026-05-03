<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-surfaces

> Layer-shell surface manager with synchronous QML loading and Vulkan wiring.

## Responsibility

Higher-level surface manager built on top of [`phosphor-layer`](../phosphor-layer/README.md).
Given a `SurfaceConfig`, `SurfaceManager::createSurface()` warms up a QML
scene, creates the layer-shell window, attaches a caller-owned or
library-managed `QVulkanInstance`, and hands back a ready-to-show
`Surface*`. This is what app code actually instantiates when it needs a
zone overlay, a drag ghost, or any layer-shell QML scene.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorSurfaces::SurfaceManager`       | Factory and owner for layer-shell surfaces. |
| `PhosphorSurfaces::SurfaceManagerConfig` | `QQmlEngine` hook, pipeline-cache path, Vulkan instance, Vulkan API version. |

## Typical use

```cpp
using namespace PhosphorSurfaces;

SurfaceManagerConfig cfg;
cfg.surfaceFactory = factory;                          // from phosphor-layer
cfg.engineConfigurator = [](QQmlEngine& e) { /* imports, context props */ };
cfg.pipelineCachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

SurfaceManager mgr(std::move(cfg));
PhosphorLayer::SurfaceConfig surfaceCfg = /* screen, role, qml URL */;
auto* surface = mgr.createSurface(surfaceCfg, /*parent*/ this);
```

## Design notes

- **Synchronous QML loading only.** Callers pass `qrc:/` or `file:/` URLs
  that resolve without a network hop. Async QML load paths are rejected
  at construction so surfaces never hand back a half-warmed window.
- **Caller-owned Vulkan, with a fallback.** If
  `SurfaceManagerConfig::vulkanInstance` is non-null, every window gets
  `setVulkanInstance()` called with that pointer. If null and the active
  Qt graphics API is Vulkan, `SurfaceManager` creates and owns a
  fallback instance internally.
- **Pipeline cache is shared.** One on-disk pipeline cache across all
  surfaces created through this manager, so shader-effect warmup on the
  second surface is essentially free.

## Dependencies

- `QtCore`, `QtQuick`, `QtQml`
- [`phosphor-layer`](../phosphor-layer/README.md) — `Surface` / `SurfaceFactory` / transport primitives

## See also

- [`phosphor-layer`](../phosphor-layer/README.md) — lower-level primitives this manager wraps.
