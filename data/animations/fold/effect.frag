// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fold fragment shader — samples the origami-folded window content.
//
// The vertex stage (effect.vert) does the accordion deformation and hands
// each fragment its sampling card uv, a per-crease shade factor, and the
// old->new cross-fade amount through vFold. This stage samples the window
// at the card uv, cross-fades the captured old frame into the live content
// as the move settles (so a resize re-lays correctly), multiplies in the
// crease shade to sell the fold, and masks the window's [0, 1] card rect.

#ifdef PLASMAZONES_KWIN
// .xy = sampling card uv, .z = crease shade, .w = old->new cross-fade.
layout(location = 1) in vec4 vFold;
#endif

#include <anchor_remap.glsl>

// uOldWindow + oldColor(): the shared captured-old-frame sampler.
#include <old_content.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    vec2 cuv = vFold.xy;
    float shade = clamp(vFold.z, 0.0, 1.0);
    float fade = clamp(vFold.w, 0.0, 1.0);

    // Feathered window mask in card space.
    vec2 fw = max(fwidth(cuv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, cuv), smoothstep(vec2(0.0), fw, 1.0 - cuv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        return vec4(0.0);
    }

    vec4 oldC = oldColor(cuv);     // captured old frame, native aspect
    vec4 newC = surfaceColor(cuv); // live new content, native aspect

    // Cross-fade old -> new as the move settles, then apply the crease
    // shade. Inputs are premultiplied, so a straight mix and a scalar
    // multiply are both correct on premultiplied colour.
    return mix(oldC, newC, fade) * shade * mask;
#else
    return surfaceColor(anchorRemap(uv));
#endif
}
