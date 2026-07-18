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
// so we use the niri OPEN body with `niri_clamped_progress` translated
// to `clamp(iTime, 0.0, 1.0)` and the runtime flip auto-mirrors the
// visual on close — no iIsReversed branch needed. One timeline
// deviation from the verbatim body: see the boundary comment below.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

#include <noise.glsl>

// The 5-octave, lacunarity-2.1 fBm over niriNoise this pack used as is_fbm
// is now the shared fbm(p, 5, 2.1) from noise.glsl, called inline below.

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
    float blob = fbm(uv * p_blobScale * screenScale, 5, 2.1);
    float fingers = fbm(uv * p_fingerScale * screenScale, 5, 2.1);
    float distortion = (blob - 0.5) * 0.5 + (fingers - 0.5) * 0.18;
    vec2 c = uv - vec2(0.5);
    float aspx = iAnchorSize.x / max(iAnchorSize.y, 0.0001);
    c.x *= aspx;
    float d = length(c);
    float splash_d = d + distortion;
    // Deviation from the verbatim niri body: the boundary's travel is
    // normalized to the farthest possible ink edge — the corner distance
    // in the same aspect metric as `d`, plus the distortion bound
    // (|distortion| <= 0.5 * 0.5 + 0.5 * 0.18 = 0.34, attained as fbm
    // approaches 0; fbm(p, 5, 2.1) tops out near 0.97 so the positive
    // side stays under +0.32) and the feather. Niri's bare
    // `p * speed - 0.15` with speed 1.7 was tuned near a full-screen
    // 16:9 surface (last finger lands ≈ 0.91);
    // the corner metric shrinks as the window narrows, so on a square
    // window the splash was done by p ≈ 0.72 and the tail sat on a
    // static frame — the phosphor-peek dead-domain bug, aspect-
    // conditioned like the desktop-phosphor projection. With the extent
    // factored out, splashSpeed = 1 (the new default) lands the last
    // finger exactly at the end of the leg for any window shape; above 1
    // it completes early and holds, wave-warp's documented front-speed
    // contract. The -0.15 head bias (niri's) is preserved on both sides
    // so t = 0 renders identically.
    float maxD = 0.5 * length(vec2(aspx, 1.0)) + 0.34 + p_edgeSoftness;
    float boundary = p * p_splashSpeed * (maxD + 0.15) - 0.15;
    float diff = splash_d - boundary;
    float reveal = smoothstep(p_edgeSoftness, -p_edgeSoftness, diff);

    return win * reveal;
}
