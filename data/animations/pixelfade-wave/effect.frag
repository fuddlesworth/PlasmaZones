// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pixelfade Wave transition — wave-front pixelation where block size
// pulses along a diagonal wave that sweeps across the surface. Inspired by
// liixini/shaders' niri pixelfade-wave shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots.
// `p_peakBlocks` is the chunkiest endpoint of the wave (fewest blocks
// across the surface = biggest visual blocks at the wave crest);
// `p_baselineBlocks` is the finest endpoint (idle resolution between
// crests). The names follow visual semantics rather than the
// numerical min/max — `p_peakBlocks` (default 8) is < `p_baselineBlocks`
// (default 800) because more blocks = smaller blocks.
// p_waveSlope is customParams[0].z.

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(iTime, 0.0, 1.0);

    float wave_x = (uv.x + uv.y) * 0.5;
    float wave_p = smoothstep(0.0, 1.0, p * 1.6 - wave_x * p_waveSlope);
    float bump = sin(wave_p * 3.14159);
    // `peakBlocks` / `baselineBlocks` mean "blocks across the screen":
    // multiplying by iAnchorSize/iSurfaceScreenPos.zw scales the count
    // to the fraction of the screen this surface covers, so block
    // pixel size stays constant across popup vs. maximized windows.
    // The multiplier is 1.0 when the surface fills the screen.
    float blocksRef = mix(p_baselineBlocks, p_peakBlocks, bump);
    vec2 blocks = vec2(blocksRef) * max(iAnchorSize, vec2(1.0))
                                  / max(iSurfaceScreenPos.zw, vec2(1.0));
    vec2 q = floor(uv * blocks) / blocks + 0.5 / blocks;

    // boundaryMask (see noise.glsl) crops the right/bottom-edge cell
    // whose centre can exceed 1.0 by up to half a cell. Without it,
    // uTexture0's clamp-to-edge sampler returns the last-column /
    // last-row texel for that cell, smearing the window's edge alpha
    // into a ~½-cell-wide band past the surface boundary.
    vec4 win = surfaceColor(q) * boundaryMask(q);

    float reveal = smoothstep(0.0, 1.0, wave_p);
    return win * reveal;
}
