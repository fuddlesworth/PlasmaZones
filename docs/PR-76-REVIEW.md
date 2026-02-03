# PR #76 Review: RHI/Vulkan zone overlay, drop OpenGL path

Review for bugs, edge cases, and .cursorrules compliance.

---

## Summary

- **Verdict**: Approve with minor notes. No blocking bugs; a few edge-case and style notes.
- **.cursorrules**: Compliant (SPDX, `#pragma once`, `QStringLiteral`/`QLatin1String`, namespace, no raw JSON keys in new C++).

---

## .cursorrules compliance

- **SPDX**: All new/edited C++, QML, and shader files have SPDX headers. ✓
- **Headers**: `#pragma once` used in `zoneshadercommon.h`, `zoneshadernodebase.h`, `zoneshadernoderhi.h`. ✓
- **Qt6 string literals**: `zoneshadernoderhi.cpp` uses `QStringLiteral()` for error messages; `shaderregistry.cpp` uses `QLatin1String()` for JSON keys and `QStringLiteral()` for defaults; `zoneshaderitem.cpp` uses `QStringLiteral()` and `QLatin1String("qrc")`. ✓
- **Namespace**: All new C++ is in `namespace PlasmaZones`. ✓
- **Naming**: Classes PascalCase, members `m_camelCase`, constants in `RhiConstants` namespace. ✓
- **Memory**: RHI resources use `std::unique_ptr`; no manual `delete` of QObjects. ✓
- **QUuid**: No UUID-to-string in this PR; shader IDs already use `shaderNameToUuid(...).toString()`. ✓

---

## Bugs and logic

### 1. No bugs found in RHI path

- `prepare()`: Early exits when `!m_item`, `!window()`, `!rhi()`, `!cb`, `!rt`; creates VBO/UBO once; bakes shaders when dirty; invalidates pipeline when render pass format changes; uploads UBO and VBO. Logic is consistent.
- `syncUniformsFromData()`: Division by zero avoided for `iMouse` when `m_width`/`m_height` are 0. Zone data and custom params/colors filled correctly; unused zone slots zeroed.
- `setHighlightedZones`: Uses zone **indices**; `indices.contains(i)` is correct for the current API (index-based highlight). ✓
- `releaseRhiResources()`: Clears pipeline, SRB, UBO, VBO, shaders, and flags; next `prepare()` will re-create as needed. ✓

### 2. Shader load path

- Vertex path is derived from the fragment path’s directory with fallback `zone.vert` → `zone.vert.glsl`. Registry defaults (`effect.frag`, `zone.vert`) and `ZoneShaderItem`’s derivation are aligned; legacy user shaders with only `zone.vert.glsl` still work. ✓
- Registry does **not** require the vertex file to exist when listing shaders (only fragment is checked). That’s acceptable because the item does its own vertex path resolution and fallback at load time.

---

## Edge cases and minor issues

### 1. Vertex shader missing message (fixed)

**File**: `src/daemon/rendering/zoneshaderitem.cpp`

When the vertex shader is missing, the warning now distinguishes: if `vertPath.isEmpty()` we log that the path could not be derived (fragment path empty); otherwise we log the directory where `zone.vert` / `zone.vert.glsl` were expected, so logs are clear in both cases.

### 2. Shader bake on render thread

Baking is done in `prepare()` (scene graph / render thread). For large or complex shaders this could cause a short stall. Acceptable for this PR; `docs/SHADER_IMPROVEMENTS.md` already mentions optional bake caching and pre-load validation as future work.

### 3. `m_initialized` not reset on VBO/UBO create failure

**File**: `zoneshadernoderhi.cpp`, `prepare()`

If `m_vbo->create()` or `m_ubo->create()` fails, the code sets `m_shaderError` and returns but does **not** set `m_initialized = false`. So on the next frame `m_initialized` is still true and the failed buffer is not re-created. That’s correct: we don’t want to retry every frame; the error is sticky until something (e.g. context loss) calls `releaseRhiResources()`. No change required.

### 4. Git rename history for shared `zone.vert` (cosmetic)

Several `zone.vert` files were introduced via renames from **other** shader directories (e.g. `toxic-circuit/zone.vert.glsl` → `aurora-sweep/zone.vert`). The **content** of the simple pass-through vertex shaders is identical after the #version 450 conversion, so behavior is correct. Only the git history is a bit confusing. Optional follow-up: in a later commit, add a brief note in a shader README or in `SHADER_IMPROVEMENTS.md` that shared pass-through `zone.vert` content is identical across packs except for magnetic-field (custom varyings).

---

## Other checks

- **Magnetic-field**: Custom vertex shader with `vMouseInfluence`, `vDistortAmount`, `vDisplacement` is correctly converted to `layout(location = 1..3) out`; fragment uses `layout(location = 1..3) in`. No extra vertex attributes are needed; those values are derived from uniforms. ✓
- **Packaging**: Arch/Debian/RPM dependency names verified (qt6-shadertools, qt6-shadertools-dev, libqt6shadertools6, qt6-qtshadertools-devel, qt6-qtshadertools). ✓
- **CMake**: `*.vert` and `*.frag` are installed; `Qt6::GuiPrivate` and `Qt6::ShaderToolsPrivate` are required. ✓
- **Docs**: `SHADER_IMPROVEMENTS.md` and code comments accurately describe RHI-only behaviour. ✓

---

## Recommendation

- **Approve** for merge once CI is green.
- The "Required vertex shader not found" log has been improved (see Edge case 1 above).
