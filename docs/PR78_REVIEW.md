# PR #78 – Multi-pass shaders: full review

Review of [PR #78](https://github.com/fuddlesworth/PlasmaZones/pull/78) (feat/multi-pass-shaders) for bugs, edge cases, and .cursorrules compliance.

---

## Summary

The PR adds multi-pass shader support: up to 4 buffer passes (A→B→C→D), optional ping-pong feedback for a single buffer pass, `bufferShaderPaths`, `bufferScale`, `bufferWrap`, and `iChannelResolution` in the UBO. Implementation touches ShaderRegistry, OverlayService, RenderNodeOverlay.qml, ZoneShaderItem, ZoneShaderNodeBase, and ZoneShaderNodeRhi.

---

## .cursorrules compliance

- **SPDX headers:** Present and correct in all modified C++/QML/shaders.
- **`#pragma once`:** Used in headers.
- **Namespace:** `PlasmaZones` used.
- **Qt6 strings:** `QLatin1String()` used for JSON keys and string comparisons; `QStringLiteral()` for paths and constants (e.g. `zoneshadernoderhi.cpp`, `shaderregistry.cpp`, `zoneshaderitem.cpp`).
- **Signals:** Setters only emit when value actually changes (e.g. `setBufferFeedback`, `setBufferScale`, `setBufferWrap`, `ZoneShaderItem` setters).
- **Q_EMIT:** Used (e.g. `ZoneShaderItem::setITime`).
- **Naming:** PascalCase classes, camelCase methods, `m_` members; signals past tense.

No .cursorrules violations found.

---

## Bugs and critical issues

### 1. Single-buffer: buffer created in `render()`, UBO has wrong `iChannelResolution` in prepare (recommended fix)

**Location:** `zoneshadernoderhi.cpp` – `prepare()` vs `render()`.

**Issue:** For single-buffer mode, `ensureBufferTarget()` is only called in `prepare()` when `multiBufferMode` is true (line 378). So for single-buffer, `m_bufferTexture` is still null during `prepare()`. Then `syncUniformsFromData()` runs and sets `iChannelResolution[0]` to `0,0` (because `m_bufferTexture` is null). The UBO is uploaded with those values. The buffer is only created later in `render()` via `ensureBufferTarget()`. The UBO is not updated again for single-buffer in `render()` (multi-buffer path does update it at 410–421). So the image pass runs with correct texture binding but `iChannelResolution[0] == vec2(0,0)`. Shaders that use it (e.g. `fragCoord / max(iChannelResolution[0], vec2(1.0))`) avoid division by zero but get wrong UVs (effectively 1×1).

**Recommendation:** Create the buffer in `prepare()` for single-buffer as well so that `syncUniformsFromData()` sees the real texture size. For example, change the condition to:

```cpp
if (!m_bufferPath.isEmpty() && bufferReady && !ensureBufferTarget()) {
    return;
}
```

So both single- and multi-buffer create targets in `prepare()` when `bufferReady`, and `iChannelResolution` is correct before the first image pass.

---

### 2. Buffer shader compile failure: no retry (minor)

**Location:** `zoneshadernoderhi.cpp` – multi-buffer block (~299–338) and single-buffer block (~339–368).

**Issue:** When multi-buffer baking fails (e.g. one of the buffer shaders fails to compile), the code sets `m_multiBufferShaderDirty = false` at the start of the block and never sets it back to `true` on failure. So the next `prepare()` does not retry. The user would need to change paths or trigger another dirty to get a retry. Same idea for single-buffer: if `m_bufferFragmentShader.isValid()` is false, `m_bufferShaderDirty` is not set true again.

**Recommendation:** Either:
- Set `m_multiBufferShaderDirty = true` (and for single-buffer `m_bufferShaderDirty = true`) when bake/compile fails so the next frame retries, or
- Document that retry only happens when paths/source change, and optionally expose a way to “retry compilation” from the UI.

---

## Edge cases and robustness

### 3. `ZoneShaderItem`: `bufferShaderPath` vs `bufferShaderPaths` can desync

**Location:** `zoneshaderitem.cpp` – `setBufferShaderPath()` and `setBufferShaderPaths()`.

**Issue:**  
- `setBufferShaderPath(path)` only updates `m_bufferShaderPath`, not `m_bufferShaderPaths`.  
- `setBufferShaderPaths(paths)` only updates `m_bufferShaderPaths`, not `m_bufferShaderPath`.

So you can have e.g. `bufferShaderPath == "a.frag"` and `bufferShaderPaths == ["x.frag","y.frag"]`. Rendering uses `effectivePaths` (paths if non-empty, else `[bufferShaderPath]`), so behavior is correct, but the `bufferShaderPath` property can be stale if only `bufferShaderPaths` is set from C++/QML.

**Recommendation (optional):** When `setBufferShaderPaths(paths)` is called, set `m_bufferShaderPath = trimmed.isEmpty() ? QString() : trimmed.constFirst()` and emit `bufferShaderPathChanged()` if it changed, so the single-path property stays in sync for UI/binding. Similarly, consider setting `m_bufferShaderPaths = path.isEmpty() ? QStringList() : QStringList{path}` in `setBufferShaderPath(path)` if you want “single path” to fully override the list (current design is that non-empty `bufferShaderPaths` wins, which is fine).

### 4. Multi-buffer: partial failure leaves stale per-pass state

**Location:** `zoneshadernoderhi.cpp` – multi-buffer bake loop.

**Issue:** If the loop fails at pass `i`, passes `0..i-1` have already had `m_multiBufferFragmentShaderSources[j]` and shaders set; pass `i` may have empty or failed source; passes `i+1..` are unchanged (possibly from a previous run). So state is mixed. Since `m_multiBufferShadersReady` stays false, we don’t use these pipelines. If you later add retry (see item 2), consider resetting all `m_multiBufferFragmentShaderSources` and related state when starting the bake so a failed run doesn’t leave half-updated data.

### 5. `ensureBufferTarget()` when size is zero

**Location:** `zoneshadernoderhi.cpp` – `ensureBufferTarget()`.

**Behavior:** When `m_width <= 0 || m_height <= 0`, the function returns `true` without creating resources. `multipassSingle` / `multipassMulti` then stay false because textures are null, so no buffer or image pass runs. This is correct.

### 6. Shader cache key and NUL

**Location:** `zoneshadernoderhi.cpp` – `shaderCacheKey()`.

**Behavior:** Key uses `kShaderCacheKeyDelim = '\0'`. Paths cannot contain NUL on supported platforms, so this is safe.

### 7. `m_dataVersion` / `m_zoneData.version`

**Location:** `zoneshaderitem.cpp` – `parseZoneData()`.

**Behavior:** `m_zoneData.version = ++m_dataVersion` is done under `m_zoneDataMutex`. The atomic increment and the copy into the snapshot are consistent. No issue.

---

## Redundant work (non-blocking)

### 8. `iChannelResolution` uploaded twice for multi-buffer

**Location:** `zoneshadernoderhi.cpp` – `prepare()` and `render()`.

**Observation:** For multi-buffer, `syncUniformsFromData()` in `prepare()` sets and uploads `iChannelResolution` (via the scene-data or full UBO update). In `render()` we again set `m_uniforms.iChannelResolution` and do a `updateDynamicBuffer` for that region. Values are the same, so this is redundant. You could skip the update in `render()` for multi-buffer if you’re sure the UBO already has the correct values from `prepare()`.

---

## Positive notes

- Single-buffer ping-pong and multi-buffer pass ordering (pass i samples 0..i-1) are implemented correctly.
- `effectivePaths` in `ZoneShaderItem::updatePaintNode()` correctly prefers `bufferShaderPaths` and falls back to `[bufferShaderPath]`.
- OverlayService sets both `bufferShaderPath` and `bufferShaderPaths` (and other buffer props) from `ShaderInfo`, so overlay and item stay in sync.
- ShaderRegistry caps buffer paths at 4 and validates multipass + buffer shaders.
- `common.glsl` uses `max(rectSize, vec2(0.001))` and similar to avoid division by zero.
- RHI resource lifecycle (create/resize/reset) and SRB invalidation when buffer textures are created/resized are handled so the image pass sees the correct textures (after fixing single-buffer creation in prepare as in item 1).

---

## Checklist

| Item | Severity | Action | Status |
|------|----------|--------|--------|
| Single-buffer: create buffer in prepare() so iChannelResolution is correct | **High** | Change condition so `ensureBufferTarget()` is called in prepare() when `bufferReady` (single or multi). | **Fixed** |
| Retry on buffer shader compile failure | Low | Set dirty flags on failure for retry next frame. | **Fixed** |
| bufferShaderPath / bufferShaderPaths sync | Low | Keep both in sync when either setter is called. | **Fixed** |
| Multi-buffer partial failure state | Low | Clear per-pass sources/shaders at start of bake. | **Fixed** |
| Double UBO upload for iChannelResolution (multi-buffer) | Nit | Remove redundant update in render(). | **Fixed** |

---

## Files reviewed

- `src/daemon/rendering/zoneshadernoderhi.cpp` / `.h`
- `src/daemon/rendering/zoneshaderitem.cpp` / `.h`
- `src/daemon/rendering/zoneshadernodebase.h`
- `src/daemon/rendering/zoneshadercommon.h`
- `src/core/shaderregistry.cpp` / `.h`
- `src/daemon/overlayservice.cpp` (buffer-related paths)
- `src/ui/RenderNodeOverlay.qml`
- `data/shaders/common.glsl`
- Sample shaders/metadata: `aretha-shell/effect.frag`, `prism-labels/metadata.json`
