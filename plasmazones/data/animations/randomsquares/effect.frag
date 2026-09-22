// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Random Squares transition — a random-grid block reveal where each cell
// flips on at its own threshold with a soft window. Inspired by
// liixini/shaders' niri randomsquares shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

// metadata.json declaration order → customParams[0] sub-slots:
// p_gridDensity (customParams[0].x), p_cellSmoothness (customParams[0].y).

// classicHash (the (12.9898, 78.233) sin-dot value hash) hosted in
// shared/noise.glsl; the per-shader rs_rand copy collapsed to it.
#include <noise.glsl>

vec4 pTransition(vec2 uv, float t) {
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
