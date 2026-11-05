// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Overexposure transition — a brightness-pumped fade where channel
// intensities lift over a sin-modulated envelope while alpha fades.
// Inspired by liixini/shaders' niri overexposure shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.
//
// The `win.r * win.a * to_m` intensity term keeps the extra `* win.a`
// deliberately. It was originally suspected of squaring alpha (our
// `uTexture0` is premultiplied), but a side-by-side trace showed the
// difference is invisible on opaque window content (the dominant case)
// and only affects translucent edges, where dropping the `* win.a` makes
// the effect noticeably brighter. The visual at peak (`to_m` ≈ 1.18)
// clamps to 1.0 in the framebuffer either way, so the over-exposure flash
// is unaffected.

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);
    float PI = 3.141592653589793;

    vec4 win = surfaceColor(uv);

    float to_m = p + sin(PI * p) * p_strength;
    return vec4(
        win.r * win.a * to_m,
        win.g * win.a * to_m,
        win.b * win.a * to_m,
        win.a * p
    );
}
