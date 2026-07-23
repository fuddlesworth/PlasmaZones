// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Flyeye transition — a faceted-lens distortion where the UV oscillates
// on a cosine/sine grid and settles as the surface fades. Inspired by
// liixini/shaders' niri flyeye shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

#include <noise.glsl>

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);

    float inv = 1.0 - p;
    vec2 disp = p_displacement * vec2(cos(p_facetFrequency * uv.x), sin(p_facetFrequency * uv.y));
    vec2 sample_uv = uv + inv * disp;

    // boundaryMask (see noise.glsl) crops samples outside [0, 1] —
    // the cos/sin facet displacement carries `sample_uv` past the
    // anchor edge along the lens facets, and uTexture0's clamp-to-
    // edge sampler would otherwise smear the rim texels into the
    // facet ripple.
    vec4 win = surfaceColor(sample_uv) * boundaryMask(sample_uv);

    return win * p;
}
