// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ink Splash transition — an ink-blot reveal where an fbm-distorted radial
// threshold blooms outward like spilled ink. Inspired by liixini/shaders'
// niri ink-splash shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. One
// timeline detail differs; see the boundary comment below. Geometry and
// texture coordinates coincide here, so `texture(uTexture0, uv)` samples
// directly.

#include <noise.glsl>

// The 5-octave, lacunarity-2.1 fBm over niriNoise this pack used as is_fbm
// is now the shared fbm(p, 5, 2.1) from noise.glsl, called inline below.

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    // `blobScale` / `fingerScale` mean "fbm cycles across the screen":
    // multiplying by iAnchorSize/iSurfaceScreenPos.zw scales the cycle
    // count to the fraction of the screen this surface covers, so
    // ink-blob and finger feature pixel size stays constant across
    // popup vs. maximized windows. The multiplier is 1.0 when the surface
    // fills the screen.
    vec2 screenScale = max(iAnchorSize, vec2(1.0)) / max(iSurfaceScreenPos.zw, vec2(1.0));
    float blob = fbm(uv * p_blobScale * screenScale, 5, 2.1);
    float fingers = fbm(uv * p_fingerScale * screenScale, 5, 2.1);
    float distortion = (blob - 0.5) * 0.5 + (fingers - 0.5) * 0.18;
    vec2 c = uv - vec2(0.5);
    float aspx = iAnchorSize.x / max(iAnchorSize.y, 0.0001);
    c.x *= aspx;
    float d = length(c);
    float splash_d = d + distortion;
    // The boundary's travel is normalized to the farthest possible ink
    // edge — the corner distance in the same aspect metric as `d`, plus
    // the distortion bound (|distortion| <= 0.5 * 0.5 + 0.5 * 0.18 = 0.34,
    // attained as fbm approaches 0; fbm(p, 5, 2.1) tops out near 0.97 so
    // the positive side stays under +0.32) and the feather. A bare
    // `p * speed - 0.15` with speed 1.7 is tuned only near a full-screen
    // 16:9 surface (last finger lands ≈ 0.91); the corner metric shrinks
    // as the window narrows, so on a square window the splash would finish
    // by p ≈ 0.72 and the tail sit on a static frame — the phosphor-peek
    // dead-domain bug, aspect-conditioned like the desktop-phosphor
    // projection. With the extent factored out, splashSpeed = 1 (the
    // default) lands the last finger exactly at the end of the leg for any
    // window shape; above 1 it completes early and holds, wave-warp's
    // documented front-speed contract. The -0.15 head bias is preserved on
    // both sides so t = 0 renders identically.
    float maxD = 0.5 * length(vec2(aspx, 1.0)) + 0.34 + p_edgeSoftness;
    float boundary = p * p_splashSpeed * (maxD + 0.15) - 0.15;
    float diff = splash_d - boundary;
    float reveal = smoothstep(p_edgeSoftness, -p_edgeSoftness, diff);

    return win * reveal;
}
