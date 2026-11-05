// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fade Color transition — a phased fade where visibility ramps from
// `p_colorPhase` onward with a smoothstep. Inspired by liixini/shaders'
// niri fadecolor shader.
//
// The phase DEFAULT is 0.0. `reveal = smoothstep(phase, 1.0, p)` draws
// literally nothing before p reaches the phase, so at 0.4 the first 40%
// of an open leg is a fully invisible window (and the last 40% of a close
// leg an invisible corpse), which an eased progress curve stretches into
// perceived launch lag (the phosphor-peek dead-domain bug). Raising the
// dial to 0.4 restores that slower, phase-delayed timing.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch.
//
// Geometry and texture coordinates coincide here, so
// `texture(uTexture0, uv)` samples directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main().

// p_colorPhase (customParams[0].x) is generated from metadata.json — no
// hand-written slot #defines.

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the reveal auto-mirrors on close
// with no direction code.
vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);

    vec4 win = surfaceColor(uv);

    float reveal = smoothstep(p_colorPhase, 1.0, p);
    return win * reveal;
}
