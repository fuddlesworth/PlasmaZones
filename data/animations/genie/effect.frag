// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Genie fragment shader — samples the grid-deformed window content.
//
// The vertex stage (effect.vert) does the minimize-to-icon deformation
// and hands each fragment its card uv and swallow progress through
// vGenie. This stage samples the window at that card uv, masks
// everything outside the window's [0, 1] card rect (the grid spans the
// whole output, but only the window is drawn), and fades the content out
// over the last stretch of the swallow so the icon-sized remnant never
// pops on the teardown frame. Restoring runs the progress the other way,
// so the same ramp fades the window in out of the icon.

#ifdef PLASMAZONES_KWIN
// .xy = card uv, .z = swallow progress (0 = window at rest, 1 = fully
// inside the icon). Interpolated from effect.vert across the grid.
// iIconRect is consumed in the vertex stage.
layout(location = 1) in vec3 vGenie;
#endif

#include <anchor_remap.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    vec2 cuv = vGenie.xy;
    float p = clamp(vGenie.z, 0.0, 1.0);

    // Feathered window mask in card space — same construction as flow.
    vec2 fw = max(fwidth(cuv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, cuv), smoothstep(vec2(0.0), fw, 1.0 - cuv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        return vec4(0.0);
    }

    float alpha = 1.0 - smoothstep(0.75, 1.0, p);

    // surfaceColor is premultiplied (KWin FBO storage); scaling the whole
    // vec4 is the correct fade.
    return surfaceColor(cuv) * (mask * alpha);
#else
    // Daemon path: the genie deformation is compositor-only. Degrade to a
    // plain fade so an assignment to an overlay show/hide leg still
    // animates. The host flips iTime on hide legs, so one expression
    // covers both directions.
    return surfaceColor(anchorRemap(uv)) * clamp(iTime, 0.0, 1.0);
#endif
}
