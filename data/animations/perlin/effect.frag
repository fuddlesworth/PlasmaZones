// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Perlin transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/perlin). Perlin-noise
// threshold reveal — surface appears where noise crosses the rising
// progress threshold.
//
// Niri's perlin ships symmetric close.glsl/open.glsl — bodies are
// identical apart from `pr = niri_clamped_progress` vs
// `pr = 1.0 - niri_clamped_progress`, so the open leg is the close
// played in reverse. PlasmaZones already flips iTime on reverse legs
// (1→0 on close, 0→1 on open), so we use the niri OPEN body verbatim
// with `niri_clamped_progress` translated to `clamp(iTime, 0.0, 1.0)`
// and the runtime flip auto-mirrors the visual on close. No
// `iIsReversed` branch required.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

// metadata.json declaration order → customParams[0] sub-slots:
// p_noiseScale (customParams[0].x), p_edgeSoftness (customParams[0].y).

// File-scope helpers kept verbatim from niri's source rather than
// substituted with `<noise.glsl>`'s `hash22` / `simplex2D`. Niri's
// `perlin_random` uses `mod(dt, 3.14)` (a low-precision pi) before the
// `sin(...) * c` step, which produces a subtly different hash
// distribution than the shared `hash22`. Replacing with the shared
// helper would alter the visual at default parameters and lose
// niri-equivalence; we keep the per-shader copies for bit-exact port
// fidelity. Same rationale applies to the bilinear `perlin_noise` body
// below — `simplex2D` would change the texture pattern.
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
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float pr = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    // `noiseScale` means "noise cycles across the screen": multiplying
    // by iAnchorSize/iSurfaceScreenPos.zw scales the cycle count to
    // the fraction of the screen this surface covers, so noise blob
    // pixel size stays constant across popup vs. maximized windows.
    // Matches niri's reference on full-screen (multiplier = 1.0 there).
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
