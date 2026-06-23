// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pixelfade Wave transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/pixelfade-wave).
// Wave-front pixelation — block size pulses along a diagonal wave
// that sweeps across the surface.
//
// Niri's pixelfade-wave ships symmetric close.glsl/open.glsl. PlasmaZones'
// runtime flips iTime on reverse legs (1→0 on close, 0→1 on open),
// so we use the niri OPEN body verbatim with `niri_clamped_progress`
// translated to `clamp(iTime, 0.0, 1.0)` and the runtime flip
// auto-mirrors the visual on close — no iIsReversed branch needed.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

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
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);

    float wave_x = (uv.x + uv.y) * 0.5;
    float wave_p = smoothstep(0.0, 1.0, p * 1.6 - wave_x * p_waveSlope);
    float bump = sin(wave_p * 3.14159);
    // `peakBlocks` / `baselineBlocks` mean "blocks across the screen":
    // multiplying by iAnchorSize/iSurfaceScreenPos.zw scales the count
    // to the fraction of the screen this surface covers, so block
    // pixel size stays constant across popup vs. maximized windows.
    // Matches niri's reference on full-screen (multiplier = 1.0 there).
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
