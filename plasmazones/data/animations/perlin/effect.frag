// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Perlin transition — a Perlin-noise threshold reveal where the surface
// appears as noise crosses the rising progress threshold. Inspired by
// liixini/shaders' niri perlin shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

// metadata.json declaration order → customParams[0] sub-slots:
// p_noiseScale (customParams[0].x), p_edgeSoftness (customParams[0].y).

// File-scope `perlin_random` / `perlin_noise` helpers are kept local
// rather than substituted with `<noise.glsl>`'s `hash22` / `simplex2D`.
// `perlin_random` uses `mod(dt, 3.14)` (a low-precision pi) before the
// `sin(...) * c` step, which produces a subtly different hash
// distribution than the shared `hash22`; swapping in the shared helper
// would change the visual at the default parameters. The same applies to
// the bilinear `perlin_noise` body below, where `simplex2D` would alter
// the texture pattern.
float perlin_random(vec2 co) {
    float a = 12.9898;
    float b = 78.233;
    float c = 43758.5453;
    float dt = dot(co.xy, vec2(a, b));
    float sn = mod(dt, 3.14);
    return fract(sin(sn) * c);
}

float perlin_noise(in vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    float a = perlin_random(i);
    float b = perlin_random(i + vec2(1.0, 0.0));
    float c = perlin_random(i + vec2(0.0, 1.0));
    float d = perlin_random(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) +
        (c - a) * u.y * (1.0 - u.x) +
        (d - b) * u.x * u.y;
}

vec4 pTransition(vec2 uv, float t) {
    float pr = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    // `noiseScale` means "noise cycles across the screen": multiplying
    // by iAnchorSize/iSurfaceScreenPos.zw scales the cycle count to
    // the fraction of the screen this surface covers, so noise blob
    // pixel size stays constant across popup vs. maximized windows.
    // The multiplier is 1.0 when the surface fills the screen.
    vec2 perScreenScale = p_noiseScale * max(iAnchorSize, vec2(1.0))
                                     / max(iSurfaceScreenPos.zw, vec2(1.0));
    float n = perlin_noise(uv * perScreenScale);
    float p = mix(-p_edgeSoftness, 1.0 + p_edgeSoftness, pr);
    float lower = p - p_edgeSoftness;
    float higher = p + p_edgeSoftness;
    float q = smoothstep(lower, higher, n);
    float reveal = 1.0 - q;

    return win * reveal;
}
