<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-rendering

> `ShaderEffect` / `ShaderNodeRhi` / `ShaderCompiler` infrastructure for
> mounting per-frame shader passes inside a Qt Quick scene graph, plus the
> zone-aware UBO extension used by the overlay.

## Responsibility

Qt Quick's built-in `ShaderEffect` doesn't support multipass, compute
shaders, custom UBO layouts, or shader-file `#include`. This library
replaces it with three cooperating pieces:

- **`ShaderEffect`** — a `QQuickItem` subclass that owns one render node,
  exposes shader source and parameters as `Q_PROPERTY`s, and delegates
  uniform packing to a pluggable `IUniformExtension` (declared in
  [`phosphor-shaders`](../phosphor-shaders/README.md)).
- **`ShaderNodeRhi`** — the scene-graph render node. Owns the QRhi
  pipeline, vertex and index buffers, the uniform buffer object (UBO),
  texture bindings, and per-pass targets. Supports multipass via
  ping-pong buffers, input-channel textures (`iChannel0..3`,
  Shadertoy-style), and writeable depth attachments.
- **`ShaderCompiler`** — a runtime GLSL to SPIR-V compiler using
  `Qt6::ShaderToolsPrivate`. Feeds into Qt's shader pipeline. Caches
  compiled modules keyed on source-hash and target-API, so re-entering
  the editor doesn't recompile unchanged shaders.

A second pair of types specialises the base node for the **zone-overlay**
case: `ZoneShaderNodeRhi` adds a labels texture binding and zone counts
in the base UBO, and `ZoneUniformExtension` writes zone rects, fill and
border colours, per-zone parameters, and the logical-to-device scale for the
lengths among them (corner radius and border width) into the UBO tail. They are
optional. Non-zone shader effects use `ShaderEffect` + `ShaderNodeRhi`
directly.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorRendering::ShaderEffect`         | The QQuickItem you instantiate in QML |
| `PhosphorRendering::ShaderNodeRhi`        | The QRhi-backed scene-graph node it owns |
| `PhosphorRendering::ShaderCompiler`       | GLSL to SPIR-V pipeline with on-disk cache |
| `PhosphorRendering::ZoneShaderNodeRhi`    | Zone-aware subclass: labels texture + zone counts in `BaseUniforms::appField0/1` |
| `PhosphorRendering::ZoneShaderUniforms`   | The GLSL-matching UBO struct, with `MaxZones` and the offset asserts, in `ZoneShaderCommon.h` |
| `PhosphorRendering::ZoneUniformExtension` | `IUniformExtension` writing zone rects / colours / params / scale |

## Typical use

This library does not ship a QML module. A consumer registers
`ShaderEffect` (and any subclass) with `qmlRegisterType()` under its own
module URI, then instantiates it in QML:

```cpp
qmlRegisterType<PhosphorRendering::ShaderEffect>("MyApp.Shaders", 1, 0, "ShaderEffect");
```

```qml
import MyApp.Shaders 1.0

ShaderEffect {
    anchors.fill: parent
    shaderSource: "qrc:/shaders/neon-city/effect.frag"
    bufferShaderPaths: [
        "qrc:/shaders/neon-city/buffer.frag"
    ]

    // Numbered vec4 / color slots packed into the UBO (customParams1..8,
    // customColor1..16):
    customParams1: Qt.vector4d(0.7, 1.0, 0.35, 0.0)
    customColor1: "#3B82F6"
    customColor2: "#A855F7"
}
```

A `setUniformExtension()` method (C++ side) attaches an
`IUniformExtension` such as `ZoneUniformExtension` to feed per-zone data
into the UBO tail.

## Design notes

- **No texture ownership by the item.** Textures are loaded by
  `ShaderNodeRhi` inside the render thread. The item just carries paths
  and parameters. This avoids the GPU-resource-lifetime bugs that plague
  naive `ShaderEffect` reimplementations.
- **Multipass is opt-in.** Single-pass shaders don't pay for the
  additional framebuffers. Setting `bufferShaderPaths` to a non-empty
  list enables the multipass path.
- **No direct GL calls.** Everything goes through QRhi, so the same code
  runs on OpenGL, Vulkan, and Metal backends. Shaders are authored in
  Vulkan-flavor GLSL 450.
- **UBO is `BaseUniforms` + extension.** The base layout from
  [`phosphor-shaders`](../phosphor-shaders/README.md) is Shadertoy-compatible,
  and consumers attach an `IUniformExtension` to append application-specific
  data. Zone rendering uses `ZoneUniformExtension`, and animations use a
  different one declared in [`phosphor-animation`](../phosphor-animation/README.md).

## Dependencies

- `QtCore`, `QtGui`, `QtQuick`, `QtSvg`
- [`phosphor-shaders`](../phosphor-shaders/README.md) — `BaseUniforms`, `IUniformExtension`
- `Qt6::ShaderToolsPrivate` for runtime compilation

## See also

- [`phosphor-shaders`](../phosphor-shaders/README.md) — `ShaderRegistry`, parameter metadata, wallpaper provider, include resolver.
- [`phosphor-animation`](../phosphor-animation/README.md) — animation-shader registry layered on top of this lib's render node.
