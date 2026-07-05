// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pendulum-move fragment shader — plain decorated sampling; the swing is
// entirely the vertex stage's rigid rotation. Samples through
// surfaceColor() so the whole decoration chain swings with the window,
// with a feathered card mask for an anti-aliased rotated silhouette.

#include <anchor_remap.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    vec2 cuv = uv;
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
