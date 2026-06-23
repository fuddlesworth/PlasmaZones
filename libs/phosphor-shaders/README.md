<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-shaders

> Shader-effect registry, base UBO layout, the `IUniformExtension`
> contract, GLSL `#include` resolution, and the abstract wallpaper
> provider. The shared shader-domain pieces every consumer of
> [`phosphor-rendering`](../phosphor-rendering/README.md) builds on.

## Responsibility

The render node in `phosphor-rendering` is generic. It owns the QRhi
pipeline and a UBO of unspecified shape. `phosphor-shaders` ships the
*shape*: the std140 base UBO every shader effect inherits, the
extension contract that lets a consumer append application-specific
data after the base region, and the metadata that turns a directory of
shader files into a discoverable, parameterised effect.

Consumed by
[`phosphor-rendering`](../phosphor-rendering/README.md) (writes the base
UBO, calls `IUniformExtension::write()` for the remainder),
[`phosphor-animation`](../phosphor-animation/README.md) (whose
`AnimationShaderRegistry` reuses `MetadataPackRegistryBase`), and the
Phosphor overlay (hosts `ShaderEffect` items in QML).

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorShaders::BaseUniforms`           | std140 base UBO layout. Shadertoy-compatible block + two `appField` ints for cheap consumer-defined state |
| `PhosphorShaders::IUniformExtension`      | Contract for appending custom uniform data after `BaseUniforms`, where `extensionSize()` is fixed for the lifetime of the instance |
| `PhosphorShaders::CustomParamsKey`        | Canonical key format (`customParams<N>_<x|y|z|w>`) for the per-effect parameter sub-slots in `BaseUniforms` |
| `PhosphorShaders::ShaderRegistry`         | Discovers shader effects from search paths via metadata-pack scanning. Per-process instance, no singleton |
| `PhosphorShaders::ShaderRegistry::ParameterInfo` | Parameter declaration: name, type, default, range, UBO uniform name |
| `PhosphorShaders::ShaderIncludeResolver`  | `#include "path"` / `#include <path>` expansion with depth limit |
| `PhosphorShaders::IWallpaperProvider`     | Abstract source for the active desktop wallpaper image path |
| `PhosphorShaders::createWallpaperProvider`| Factory that auto-detects KDE / Hyprland / Sway / GNOME |

## Typical use

Wire up a shader effect in QML, with a per-process registry and a
wallpaper provider for effects that sample the desktop background:

```cpp
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorShaders/IWallpaperProvider.h>

using namespace PhosphorShaders;

ShaderRegistry registry;
registry.addSearchPath(QStringLiteral("/usr/share/myapp/shaders"));
registry.setUserPath(QStandardPaths::writableLocation(
    QStandardPaths::AppDataLocation) + QStringLiteral("/shaders"));
registry.refresh();

auto wallpaper = createWallpaperProvider();
QString path = wallpaper->wallpaperPath();   // "" if unsupported DE
```

Implement an `IUniformExtension` to append per-zone data to the UBO:

```cpp
class MyExtension : public PhosphorShaders::IUniformExtension {
public:
    int  extensionSize() const override { return sizeof(MyTail); }
    bool isDirty() const override       { return m_dirty.exchange(false); }
    void write(void* dst) override      { std::memcpy(dst, &m_tail, sizeof(MyTail)); }
private:
    MyTail m_tail;
    mutable std::atomic<bool> m_dirty{true};
};
```

## Design notes

- **No library-level singleton.** Composition roots own a per-process
  `ShaderRegistry` instance and register search paths explicitly.
  Tests construct a per-fixture registry, and downstream consumers do the
  same.
- **`extensionSize()` is fixed for the instance lifetime.** The render
  node sizes the UBO and staging buffer once when an extension is
  installed via `setUniformExtension()` and reuses both across
  frames. To resize, install a fresh extension instance, which
  triggers UBO recreation.
- **`appField0 / appField1` exist regardless of use.** They fill the
  std140 alignment slot between `iResolution` (vec2) and `iMouse`
  (vec4), and removing them would break the layout. Repurpose them for
  small (≤2 ints) frequently-updated state that needs to live inside
  `BaseUniforms` rather than the extension region.
- **GUI-thread only for reads and mutations.** The shader map lives
  inside the strategy and is rebuilt on the GUI thread inside the
  rescan. The public lookup methods (`availableShaders`, `shader`,
  `shaderInfo`, `shaderUrl`) read it without synchronisation.
  `searchPaths()` returns a by-value snapshot suitable for handing to
  worker threads (the shader-warming path).
- **Inherits `MetadataPackRegistryBase`.** Search-path management is
  in `phosphor-fsloader`, and `ShaderRegistry` adds only the
  shader-specific lookup surface.

## Dependencies

- `QtCore`, `QtGui`
- [`phosphor-fsloader`](../phosphor-fsloader/README.md) — `MetadataPackRegistryBase` + `MetadataPackScanStrategy`

## See also

- [`phosphor-rendering`](../phosphor-rendering/README.md) — `ShaderEffect` + `ShaderNodeRhi` consume `BaseUniforms` and `IUniformExtension`.
- [`phosphor-animation`](../phosphor-animation/README.md) — `AnimationShaderRegistry` is a parallel registry for transition effects.
