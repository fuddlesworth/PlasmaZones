<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-rendering

> `ShaderEffect` / `ShaderNodeRhi` / `ShaderCompiler` infrastructure for
> mounting per-frame shader passes inside a Qt Quick scene graph, plus the
> zone-aware UBO extension used by the overlay.

## Responsibility

Qt Quick's built-in `ShaderEffect` item is fine for toy demos but doesn't
support multipass, compute shaders, custom UBO layouts, or including one
shader file from another. `phosphor-rendering` replaces it with three
cooperating pieces:

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
  `glslang`. Feeds into Qt's shader pipeline. Caches compiled modules
  keyed on source-hash and target-API, so re-entering the editor doesn't
  recompile unchanged shaders.

A second pair of types specialises the base node for the **zone-overlay**
case: `ZoneShaderNodeRhi` adds a labels texture binding and zone counts
in the base UBO, and `ZoneUniformExtension` writes zone rects, fill /
border colours, and per-zone parameters into the UBO tail. They are
optional — non-zone shader effects use `ShaderEffect` + `ShaderNodeRhi`
directly.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorRendering::ShaderEffect`         | The QQuickItem you instantiate in QML |
| `PhosphorRendering::ShaderNodeRhi`        | The QRhi-backed scene-graph node it owns |
| `PhosphorRendering::ShaderCompiler`       | GLSL to SPIR-V pipeline with on-disk cache |
| `PhosphorRendering::ZoneShaderNodeRhi`    | Zone-aware subclass: labels texture + zone counts in `BaseUniforms::appField0/1` |
| `PhosphorRendering::ZoneShaderCommon`     | Shared layout constants (`MaxZones`, GLSL-matching struct) |
| `PhosphorRendering::ZoneUniformExtension` | `IUniformExtension` writing zone rects / colours / params |

## Typical use

In QML:

```qml
import PhosphorRendering 1.0

ShaderEffect {
    anchors.fill: parent
    fragmentShader: "qrc:/shaders/neon-city/effect.frag"
    bufferShaderPaths: [
        "qrc:/shaders/neon-city/buffer.frag"
    ]

    customParams: [0.7, 1.0, 0.35, 0.0]   // packed into UBO
    customColors: ["#3B82F6", "#A855F7"]

    // Optional: a uniform extension feeds per-zone data into the UBO tail
    uniformExtension: ZoneUniformExtension { zones: view.zones }
}
```

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
  [`phosphor-shaders`](../phosphor-shaders/README.md) is Shadertoy-compatible;
  consumers attach an `IUniformExtension` to append application-specific
  data. Zone rendering uses `ZoneUniformExtension`; animations use a
  different one declared in [`phosphor-animation`](../phosphor-animation/README.md).

## Dependencies

- `QtCore`, `QtGui`, `QtQuick`, `QtQml`
- [`phosphor-shaders`](../phosphor-shaders/README.md) — `BaseUniforms`, `IUniformExtension`
- `glslang` for runtime compilation

## See also

- [`phosphor-shaders`](../phosphor-shaders/README.md) — `ShaderRegistry`, parameter metadata, wallpaper provider, include resolver.
- [`phosphor-animation`](../phosphor-animation/README.md) — animation-shader registry layered on top of this lib's render node.
