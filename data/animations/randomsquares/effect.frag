// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Random Squares transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/randomsquares).
// Random-grid block reveal — each cell flips on at its own threshold
// with a soft window.
//
// Niri's randomsquares ships symmetric close.glsl/open.glsl.
// PlasmaZones' runtime flips iTime on reverse legs (1→0 on close,
// 0→1 on open), so we use the niri OPEN body verbatim with
// `niri_clamped_progress` translated to `clamp(iTime, 0.0, 1.0)` and
// the runtime flip auto-mirrors the visual on close — no iIsReversed
// branch needed.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

// metadata.json declaration order → customParams[0] sub-slots:
// p_gridDensity (customParams[0].x), p_cellSmoothness (customParams[0].y).

// classicHash (the (12.9898, 78.233) sin-dot value hash) hosted in
// shared/noise.glsl; the per-shader rs_rand copy collapsed to it.
#include <noise.glsl>

vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    // `p_gridDensity` means "cells across the screen": multiplying by
    // iAnchorSize/iSurfaceScreenPos.zw scales the count to the
    // fraction of the screen this surface covers, so cell pixel size
    // stays constant across popup vs. maximized windows. Floors guard
    // against the pre-first-frame (0,0) state of either uniform.
    vec2 sz = vec2(p_gridDensity) * max(iAnchorSize, vec2(1.0))
                                / max(iSurfaceScreenPos.zw, vec2(1.0));
    float r = classicHash(floor(sz * uv));
    float reveal = smoothstep(0.0, -p_cellSmoothness, r - (p * (1.0 + p_cellSmoothness)));

    return win * reveal;
}
