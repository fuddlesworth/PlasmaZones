// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Tab Dissolve — each speckle of the column flips from the outgoing tab to the
// incoming one at its own random threshold. The tab-quad twin of
// desktop-dissolve, and the one member of this family with no direction at all:
// the swap arrives everywhere at once, which reads well when the two tabs hold
// unrelated content and a directional sweep would imply a spatial relationship
// they do not have.
//
// Geometry and texture coordinates coincide, so the samplers take uv directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl carries uOldWindow / oldColor / oldCrossFade;
// noise.glsl carries classicHash.
#include <old_content.glsl>
#include <noise.glsl>

// p_scale / p_softness (customParams[0].xy) come from metadata.json.

vec4 pTransition(vec2 uv, float t) {
    // Speckles keyed off the UV grid rather than device pixels, so the grain
    // stays the same size relative to the column whatever its width — a tab in
    // a narrow column and one in a wide column dissolve alike.
    float n = classicHash(floor(uv * max(p_scale, 1.0)));
    float soft = max(p_softness, 1.0e-3);
    // Padded like the wipe: without it, speckles whose threshold lands within
    // `soft` of either end show a partial blend at t = 0 and t = 1 and the swap
    // starts and finishes with a visible step.
    float p = clamp(t, 0.0, 1.0) * (1.0 + 2.0 * soft) - soft;
    return oldCrossFade(uv, smoothstep(n - soft, n + soft, p));
}
