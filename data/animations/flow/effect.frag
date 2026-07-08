// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Flow fragment shader — samples the grid-deformed window content.
//
// The vertex stage (effect.vert) does the geometry deformation and hands
// each fragment its card uv and arrival ease through vFlow. This stage
// just samples the window at that card uv, cross-fading the captured old
// frame (uOldWindow) into the live new content (surfaceColor) as each
// region settles, and masks everything outside the window's [0, 1] card
// rect (the grid covers the whole output, but only the window is drawn).
//
// Old and new are sampled at the SAME card uv, so each shows at its own
// native aspect — no non-uniform stretch.

#ifdef PLASMAZONES_KWIN
// .xy = card uv within the destination rect, .z = arrival ease (0 = old
// rect, 1 = settled). Interpolated from effect.vert across the grid.
// iFromRect / iToRect are consumed in the vertex stage.
layout(location = 1) in vec3 vFlow;
#endif

#include <anchor_remap.glsl>

// uOldWindow + oldColor(): the shared captured-old-frame sampler.
#include <old_content.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    vec2 cuv = vFlow.xy;
    float e = clamp(vFlow.z, 0.0, 1.0);

    // Feathered window mask in card space — the grid spans the whole
    // output, so only cells inside [0, 1] carry window content.
    vec2 fw = max(fwidth(cuv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, cuv), smoothstep(vec2(0.0), fw, 1.0 - cuv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        return vec4(0.0);
    }

    vec4 oldC = oldColor(cuv);     // captured old frame, native aspect
    vec4 newC = surfaceColor(cuv); // live new content, native aspect

    // Cross-fade old -> new as each region arrives. Inputs are
    // premultiplied (KWin FBO storage); a straight mix is correct.
    return mix(oldC, newC, e) * mask;
#else
    // Daemon path: flow is compositor-only. Render the surface unchanged
    // so the shader bakes for the daemon target and is harmless if run.
    return surfaceColor(anchorRemap(uv));
#endif
}
