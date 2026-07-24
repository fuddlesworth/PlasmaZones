// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Colour Distance transition — a luminance-thresholded reveal where
// pixels emerge in order of brightness. Inspired by liixini/shaders'
// niri colour-distance shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main().

// p_power (customParams[0].x) is generated from metadata.json — no
// hand-written slot #defines.

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the reveal auto-mirrors on close with no
// direction code.
vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);

    vec4 win = surfaceColor(uv);

    float colorMag = length(win.rgb);
    float m = step(colorMag, p);
    float reveal = mix(m, 1.0, pow(p, p_power));

    return win * reveal;
}
