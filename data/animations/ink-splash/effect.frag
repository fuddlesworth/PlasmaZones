// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ink Splash transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/ink-splash). Ink-blot
// reveal — fbm-distorted radial threshold blooms outward like spilled
// ink.
//
// Niri's ink-splash ships symmetric close.glsl/open.glsl. PlasmaZones'
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

float is_fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; i++) {
        v += amp * niriNoise(p);
        p *= 2.1;
        amp *= 0.5;
    }
    return v;
}

vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(t, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    // `blobScale` / `fingerScale` mean "fbm cycles across the screen":
    // multiplying by iAnchorSize/iSurfaceScreenPos.zw scales the cycle
    // count to the fraction of the screen this surface covers, so
    // ink-blob and finger feature pixel size stays constant across
    // popup vs. maximized windows. Matches niri's reference on full-
    // screen (multiplier = 1.0 there).
    vec2 screenScale = max(iAnchorSize, vec2(1.0)) / max(iSurfaceScreenPos.zw, vec2(1.0));
    float blob = is_fbm(uv * p_blobScale * screenScale);
    float fingers = is_fbm(uv * p_fingerScale * screenScale);
    float distortion = (blob - 0.5) * 0.5 + (fingers - 0.5) * 0.18;
    vec2 c = uv - vec2(0.5);
    c.x *= iAnchorSize.x / max(iAnchorSize.y, 0.0001);
    float d = length(c);
    float splash_d = d + distortion;
    float boundary = p * p_splashSpeed - 0.15;
    float diff = splash_d - boundary;
    float reveal = smoothstep(p_edgeSoftness, -p_edgeSoftness, diff);

    return win * reveal;
}
