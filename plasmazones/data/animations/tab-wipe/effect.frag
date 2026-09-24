// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Tab Wipe — a directional sweep across the column, the tab-quad twin of
// desktop-wipe. Nothing translates: the two tabs sit exactly on top of one
// another and the boundary between them travels. That suits a tab swap, where
// both windows genuinely occupy the same rect and any motion is invented.
//
// Geometry and texture coordinates coincide, so the samplers take uv directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl carries uOldWindow / oldColor / oldCrossFade.
#include <old_content.glsl>

// p_dirX / p_dirY / p_softness (customParams[0].xyz) come from metadata.json.

vec4 pTransition(vec2 uv, float t) {
    // Zero-direction guard: fall back to a rightward sweep rather than
    // epsilon-nudging into normalize, where two tiny opposing components can
    // still cancel and produce a NaN.
    vec2 raw = vec2(p_dirX, p_dirY);
    vec2 dir = dot(raw, raw) > 1.0e-6 ? normalize(raw) : vec2(1.0, 0.0);

    // Projection normalised by the direction's L1 extent so proj spans exactly
    // [0,1] corner to corner for ANY direction. A plain dot()+0.5 overshoots on
    // diagonals (a unit diagonal reaches ±0.707), which would leave a corner
    // sliver unwiped at both ends. ext is >= 1 for a unit vector, so it can
    // never divide by zero, and it reduces to uv.x for (1,0).
    float ext = abs(dir.x) + abs(dir.y);
    float proj = dot(uv - vec2(0.5), dir) / ext + 0.5;

    float soft = max(p_softness, 1.0e-3);
    // Padded by the feather width so the sweep has fully cleared the rect at
    // both ends. Without it the fragments within `soft` of either corner show a
    // partial blend at t = 0 and t = 1, and the swap pops.
    float sweep = clamp(t, 0.0, 1.0) * (1.0 + 2.0 * soft) - soft;

    // proj below the sweep line has already turned over to the incoming tab.
    return oldCrossFade(uv, 1.0 - smoothstep(sweep - soft, sweep + soft, proj));
}
