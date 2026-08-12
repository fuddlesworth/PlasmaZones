// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Tab Slide — the two tabs travel together, the outgoing one leaving by one
// edge as the incoming one arrives from the opposite side.
//
// The desktop sibling (desktop-slide, GL-Transitions "directional") wraps one
// full-screen scene with fract() and reads the wrapped side as the incoming
// desktop. That trick does NOT carry over to a window quad: fract() would wrap
// each TAB onto itself, so a tab sliding half out would show its own right half
// pasted onto its left. Two independently displaced samples, each masked to its
// own bounds, is the honest form here — literally two cards passing.
//
// Geometry and texture coordinates coincide, so both samplers take uv directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl carries uOldWindow / oldColor; noise.glsl carries
// boundaryMask, which is what keeps a displaced sample from smearing its edge
// texel across the rest of the column.
#include <old_content.glsl>
#include <noise.glsl>

// p_dirX / p_dirY (customParams[0].xy) are generated from metadata.json.

vec4 pTransition(vec2 uv, float t) {
    // Clamped: a slide has no third tab to reveal, so there is nothing to
    // overshoot INTO. An overshooting curve would push both tabs off their own
    // rect and leave the column momentarily empty.
    float p = clamp(t, 0.0, 1.0);

    // Sign only, so a diagonal travels corner to corner rather than at the
    // vector's own magnitude. The all-zero guard keeps a pack whose sliders
    // are both centred from cutting instead of sliding.
    vec2 s = sign(vec2(p_dirX, p_dirY));
    if (s == vec2(0.0)) {
        s = vec2(1.0, 0.0);
    }

    // The outgoing tab walks out along s; the incoming one starts a full rect
    // back along it and lands at 0. Sampling at uv MINUS the displacement is
    // what moves the image WITH the direction.
    vec2 outUv = uv - p * s;
    vec2 inUv = uv + (1.0 - p) * s;

    // Each side is cropped to its own rect, so the part of it that has left
    // does not paint. The two masks are disjoint everywhere except the seam,
    // where their feathers overlap and sum to one — which is exactly the
    // join the eye should not be able to find.
    return oldColor(outUv) * boundaryMask(outUv) + surfaceColor(inUv) * boundaryMask(inUv);
}
