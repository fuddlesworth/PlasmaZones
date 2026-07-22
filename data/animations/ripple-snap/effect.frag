// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ripple-snap fragment shader — samples the impact-rippled window content.
//
// The vertex stage (effect.vert) does the slam + ripple deformation and
// hands each fragment its sampling card uv, a ripple-shade factor, and the
// old->new cross-fade through vRip. This stage samples the window at the
// card uv, cross-fades the captured old frame into the live content as the
// move settles (so a resize re-lays correctly), multiplies in the ripple
// shade, and masks the window's pad-widened card rect.

// .xy = sampling card uv, .z = ripple shade, .w = old->new cross-fade.
layout(location = 1) in vec4 vRip;

// uOldWindow + oldColor(): the shared captured-old-frame sampler.
#include <old_content.glsl>

vec4 pTransition(vec2 uv, float t) {
    vec2 cuv = vRip.xy;
    float shade = clamp(vRip.z, 0.0, 1.0);
    float fade = clamp(vRip.w, 0.0, 1.0);

    // Feathered window mask in card space, widened past [0, 1] by the
    // decoration chain's outer margin so the halo the compositor composited
    // into the padded canvas ripples with the window instead of being
    // cropped at the frame edge. Zero pad reduces to the bare card edge.
    vec2 pad = surfacePadRel();
    vec2 fw = max(fwidth(cuv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, cuv + pad), smoothstep(vec2(0.0), fw, 1.0 + pad - cuv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        return vec4(0.0);
    }

    vec4 oldC = oldColor(cuv);     // captured old frame, native aspect
    vec4 newC = surfaceColor(cuv); // live new content, native aspect

    // Cross-fade old -> new as the move settles, then apply the ripple
    // shade. The scalar multiply scales alpha too, so on premultiplied
    // colour the wave troughs are deliberately a coverage fade (the
    // backdrop ghosts through slightly) rather than a pigment darken —
    // and it keeps the rgb <= a premultiplied invariant intact.
    return mix(oldC, newC, fade) * shade * mask;
}
