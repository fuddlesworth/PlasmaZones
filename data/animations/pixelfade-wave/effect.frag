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

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots.
// `peakBlocks` is the chunkiest endpoint of the wave (fewest blocks
// across the surface = biggest visual blocks at the wave crest);
// `baselineBlocks` is the finest endpoint (idle resolution between
// crests). The names follow visual semantics rather than the
// numerical min/max — `peakBlocks` (default 8) is < `baselineBlocks`
// (default 800) because more blocks = smaller blocks.
#define peakBlocks      customParams[0].x
#define baselineBlocks  customParams[0].y
#define waveSlope       customParams[0].z

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// Reference card edge length for pixel-constant `peakBlocks` /
// `baselineBlocks` scaling. Both params are interpreted as "blocks
// across an 800-pixel reference card"; the iAnchorSize multiply keeps
// per-block pixel size constant on larger / smaller surfaces (default
// peakBlocks=8 → 100-pixel block chunks at the wave crest, regardless
// of window size).
const float kReferenceCardSize = 800.0;

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;

    float wave_x = (uv.x + uv.y) * 0.5;
    float wave_p = smoothstep(0.0, 1.0, p * 1.6 - wave_x * waveSlope);
    float bump = sin(wave_p * 3.14159);
    float blocksRef = mix(baselineBlocks, peakBlocks, bump);
    vec2 blocks = vec2(blocksRef) * max(iAnchorSize, vec2(1.0)) / kReferenceCardSize;
    vec2 q = floor(uv * blocks) / blocks + 0.5 / blocks;

    // boundaryMask (see noise.glsl) crops the right/bottom-edge cell
    // whose centre can exceed 1.0 by up to half a cell. Without it,
    // uTexture0's clamp-to-edge sampler returns the last-column /
    // last-row texel for that cell, smearing the window's edge alpha
    // into a ~½-cell-wide band past the surface boundary.
    vec4 win = surfaceColor(q) * boundaryMask(q);

    float reveal = smoothstep(0.0, 1.0, wave_p);
    fragColor = win * reveal;
}
