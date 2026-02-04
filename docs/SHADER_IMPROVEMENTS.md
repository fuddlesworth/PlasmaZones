# Shader Implementation Improvements

Suggested improvements to the PlasmaZones shader system. **RHI/Vulkan migration (Section 2) is complete.** Other sections are a backlog for performance, authoring, and robustness.

---

## Status: RHI migration complete (2026)

- **Zone overlay** uses **Qt RHI only** (`ZoneShaderNodeRhi`). OpenGL path (`ZoneShaderNode`, `QOpenGLShaderProgram`) has been removed.
- **Shaders** are Vulkan GLSL `#version 450`; filenames `zone.vert` / `effect.frag`. All built-in shaders converted; registry defaults and loader support both `.vert`/`.frag` and legacy `.vert.glsl`/`.glsl`.
- **Runtime pipeline**: Raw GLSL loaded from disk → **QShaderBaker** (with `setGeneratedShaders` for SPIR-V, GLSL ES/120, HLSL 50, MSL 12) → **QRhi** pipeline. No .qsb build step; no OpenGL code.
- **Dependencies**: Qt 6.6+ required; `Qt6::GuiPrivate` (QRhi) and `Qt6::ShaderToolsPrivate` (QShaderBaker) required for the daemon.

### Progress (post-migration)

- **Codebase cleanup**: All `#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)` guards and `#else` fallbacks removed from `ZoneShaderNodeRhi`; minimum Qt 6.6 is required. Unused `PLASMAZONES_RHI_AVAILABLE` compile definition removed.
- **Registry and loader**: Shader registry defaults and `ZoneShaderItem` vertex path use `effect.frag` / `zone.vert`; legacy `zone.vert.glsl` / `.glsl` kept for backward compatibility with user shaders.
- **Comments**: Outdated OpenGL references in QML and `ShaderDbusQueries` updated to describe RHI-only behaviour.

---

## 1. Performance

### Partial UBO updates ✅ (implemented)

~~Currently the full `ZoneShaderUniforms` block is uploaded every frame~~ **Implemented:** UBO updates are now partial where possible.

- **Time block only** (iTime, iTimeDelta, iFrame): when only the animation timer tick runs, we upload just that region via `ZoneShaderUboRegions::kTimeBlockOffset` / `kTimeBlockSize` (12 bytes).
- **Zone data only**: when only zone rects/colors/params/resolution/mouse/custom params change, we upload only the scene region via `kSceneDataOffset` / `kSceneDataSize`.
- **Full upload**: first frame after init or context loss still does a full upload; subsequent frames use one or two partial `updateDynamicBuffer` calls.

Constants live in `zoneshadercommon.h` (`ZoneShaderUboRegions`). Granular dirty flags (`m_timeDirty`, `m_zoneDataDirty`) are set in setters and cleared after upload. This reduces PCIe/GPU bandwidth and helps on multi-monitor setups.

**Not yet done:** Per-frame matrix/opacity from the scene graph (smaller upload when only `qt_Matrix`/`qt_Opacity` change) — would require reading `RenderState` and comparing to last uploaded values.

### Shader bake caching ✅ (implemented)

~~Shaders are baked with QShaderBaker on first use.~~ **Implemented:** In-memory cache keyed by vertex path + vertex mtime + fragment path + fragment mtime.

- On load (when both paths are set), we look up the cache in `prepare()`; on hit we reuse the baked `QShader` pair and skip bake.
- On miss we bake and insert into a static `QHash` guarded by `QMutex` (prepare runs on the render thread).
- Paths and mtimes are set in `loadVertexShader()` / `loadFragmentShader()`; when source is set directly (no path), cache is skipped.

**Optional future:** Persist cache to disk under `~/.cache/plasmazones/shaders/` for faster startup and hot-reload.

### Smarter update throttling

You already have a configurable shader frame rate.

- **Pause when overlay hidden** ✅: The overlay already calls `stopShaderAnimation()` in `hide()`, so the shader timer is stopped when not visible; no change needed.
- For **static-looking shaders** (no `iTime` usage), optionally skip or reduce timer-driven updates and only push updates when zone data or params change — not implemented (would require metadata or shader analysis).

---

## 2. RHI / Vulkan migration ✅ (completed)

The overlay now uses **Qt RHI only** via `ZoneShaderNodeRhi` (no OpenGL path). One code path for Vulkan, Metal, D3D 11/12, and OpenGL when Qt Quick uses an RHI backend.

### Implemented

- **QSGRenderNode with QRhi (option A)**: `ZoneShaderNodeRhi` in `prepare()` / `render()` uses `window()->rhi()`, `commandBuffer()`, `renderTarget()`; creates VBO, UBO, `QRhiShaderResourceBindings`, `QRhiGraphicsPipeline`; pipeline invalidated when `renderPassDescriptor()->serializedFormat()` changes.
- **Shader pipeline**: Raw Vulkan GLSL (`#version 450`) on disk (`zone.vert`, `effect.frag`). Loaded at runtime; **QShaderBaker** with `setGeneratedShaderVariants({ StandardShader })` and `setGeneratedShaders()` for SPIR-V 100, GLSL ES 100, GLSL 120, HLSL 50, MSL 12. Resulting `QShader` passed to `QRhiShaderStage`; QRhi selects the right variant.
- **ZoneShaderItem**: Always creates `ZoneShaderNodeRhi`; same data flow (zone data, time, params). Vertex path: `zone.vert` with fallback to legacy `zone.vert.glsl`.
- **Shaders**: All built-in shaders converted to `#version 450` with `layout(location=...)` and `layout(set=0, binding=0, std140) uniform ZoneUniforms`. Registry defaults: `effect.frag`, `zone.vert`.
- **OpenGL path removed**: `ZoneShaderNode` (QOpenGLShaderProgram, UBO, VAO) and `Qt6::OpenGL` dependency removed. No `#if QT_VERSION` or fallback; RHI is the only path.
- **Build**: `Qt6::GuiPrivate` and `Qt6::ShaderToolsPrivate` **required**; minimum Qt 6.6.

### Reference (for maintenance)

- **API**: [QSGRenderNode](https://doc.qt.io/qt-6/qsgrendernode.html), [RHI Under QML](https://doc.qt.io/qt-6/qtquick-scenegraph-rhiunderqml-example.html), [QShaderBaker](https://doc.qt.io/qt-6/qshaderbaker.html).
- **Uniform block**: `ZoneShaderUniforms` in C++ and `layout(set=0, binding=0, std140) uniform ZoneUniforms` in GLSL; single UBO, `updateDynamicBuffer()` in `prepare()`.
- **Pipeline invalidation**: Compare `renderPassDescriptor()->serializedFormat()`; rebuild pipeline when it changes.

---

## 3. Other backend / portability

### RHI backends

The comment says “OpenGL 3.3+ or OpenGL ES 3.0+”. If you care about GLES (e.g. some Wayland compositors):

Zone overlay uses QRhi; the active backend (Vulkan, OpenGL, Metal, D3D) is chosen by Qt Quick. QShaderBaker produces variants for all of them. No separate GLES path; GLSL ES 100 variant is generated when needed.

---

## 4. Shader authoring and validation

### Pre-load validation

Shader errors currently appear when the overlay is shown (and in the journal). Optional improvement:

- In **ShaderRegistry** or when the user selects a shader in the editor: run **QShaderBaker** on the vertex/fragment source (e.g. in a worker or on a throwaway thread). If bake() fails, surface the error in the KCM/editor and optionally mark the shader as broken.
- Optionally run after a debounce when the user shader directory changes, so broken shaders are flagged without opening the overlay.

### Shared “common” code

Many shaders duplicate `sdRoundedBox`, hash, etc. You could:

- Add a **common include** (e.g. `common.glsl` or `plasmazones.glsl`) that is concatenated or included (via a simple preprocessor or load-time string substitution) before the fragment shader. Document the provided functions (e.g. `sdRoundedBox`, `renderZone`-style helpers) so authors don’t copy-paste and drift.

---

## 5. Robustness and UX

### Context loss and re-init

`ZoneShaderNodeRhi::releaseResources()` calls `releaseRhiResources()`. Ensure that after a **context loss**:

- `m_initialized` (and related flags) are cleared so the next `prepare()` re-creates all RHI resources.
- The scene graph calls `releaseResources()` when the node is torn down so no stale QRhi objects are held.

### Safer fallback and retry

You already fall back to the non-shader overlay on error. You could:

- Remember “this shader ID failed last time” and skip trying it again until the user changes the shader or restarts (to avoid log spam and repeated compile attempts).
- Optionally retry once with a built-in “minimal” shader (e.g. minimalist) before giving up, so the user still sees zones with a simple effect.

### Live shader preview

The wiki mentions `preview.png`. For a better authoring experience:

- In the **layout editor**, when a shader is selected, show a small live preview (same `ZoneShaderItem`/node with a few dummy zones and current params) so authors see changes without toggling the overlay on the real layout.

---

## 6. Smaller cleanups

- ~~**ZoneShaderNodeRhi**: In `prepare()`, the full UBO is uploaded every time. Consider partial updates~~ ✅ Done (Section 1): partial UBO updates for time block and zone/scene data are implemented. Matrix/opacity-only partial update is optional future work.
- **ZoneShaderItem** color parsing from `shaderParams` (e.g. `customColor5`–`8`) could share a single helper (or loop) with `customColor1`–`4` to avoid duplication.
- **ShaderRegistry**: `shaderCompilationStarted` / `shaderCompilationFinished` are declared but baking runs on the render thread in `ZoneShaderNodeRhi`; if pre-load validation is added, emit those from the validation path so the UI can show a compiling state.

---

## Reference: current shader stack

| Component | Role |
|-----------|------|
| `ShaderRegistry` | Loads system + user shaders from metadata.json, maps params to uniforms, file watcher. Defaults: `effect.frag`, `zone.vert`. |
| `ZoneShaderNodeRhi` | QSGRenderNode: QRhi VBO/UBO, fullscreen quad, QShaderBaker (runtime), pipeline/srb, render. Only render node implementation. |
| `ZoneShaderNodeBase` | Abstract base for the render node (used by ZoneShaderItem). |
| `ZoneShaderItem` | QQuickItem: zone data, shader URL/params, updatePaintNode → creates ZoneShaderNodeRhi, loads zone.vert / effect.frag. |
| `OverlayService` | Creates overlay windows, shader vs standard, timer, zone data → item. |
| `data/shaders/*/` | effect.frag, zone.vert, metadata.json (and optional preview.png). Vulkan GLSL #version 450. |

See also: [PlasmaZones.wiki/Shaders](https://github.com/fuddlesworth/PlasmaZones/wiki/Shaders).
