// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Soft Warp Fade transition — a subtle noise warp combined with an eased
// fade for a gentle, organic transition. Inspired by liixini/shaders' niri
// soft-warp-fade shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly, and the `niriHash` / `niriNoise` helpers come from
// `<noise.glsl>`.

#include <noise.glsl>

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(iTime, 0.0, 1.0);

    float strength = sin(p * 3.14159) * p_warpStrength;
    // `noiseScale` means "noise cycles across the screen": multiplying
    // by iAnchorSize/iSurfaceScreenPos.zw scales the cycle count to
    // the fraction of the screen this surface covers, so warp-noise
    // pixel size stays constant across popup vs. maximized windows.
    // The multiplier is 1.0 when the surface fills the screen.
    vec2 perScreenScale = p_noiseScale * max(iAnchorSize, vec2(1.0))
                                     / max(iSurfaceScreenPos.zw, vec2(1.0));
    vec2 warp = vec2(
        niriNoise(uv * perScreenScale + vec2(0.0, p * 0.5)),
        niriNoise(uv * perScreenScale + vec2(p * 0.5, 0.0))
    ) - 0.5;
    vec2 warped = uv + warp * strength;

    // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
    vec4 win = surfaceColor(warped) * boundaryMask(warped);

    float tt = smoothstep(0.05, 0.95, p);
    tt = tt * tt * (3.0 - 2.0 * tt);
    return win * tt;
}
