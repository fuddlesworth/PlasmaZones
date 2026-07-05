// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wobbly-move fragment shader — plain decorated sampling; all the work
// happens in the vertex stage's velocity-lag deformation. Samples through
// surfaceColor() so the full decoration chain (blur pane, border, glow)
// wobbles with the window as one object, and feathers the card edge so
// the deformed silhouette stays anti-aliased.

#include <anchor_remap.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    vec2 cuv = uv;
    // Feathered card mask: the grid only spans the card, but edge
    // interpolation still benefits from an explicit soft clip.
    vec2 fw = max(fwidth(cuv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, cuv), smoothstep(vec2(0.0), fw, 1.0 - cuv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        return vec4(0.0);
    }
    return surfaceColor(cuv) * mask;
#else
    return surfaceColor(anchorRemap(uv));
#endif
}
