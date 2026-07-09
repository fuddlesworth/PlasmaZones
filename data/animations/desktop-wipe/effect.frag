// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Wipe — a soft-edged line sweeps across the screen in the p_dirX /
// p_dirY direction, revealing the incoming desktop behind it. `t` is forward
// switch progress in [0,1]; the sweep position is padded by the edge softness so
// the wipe fully clears at both ends.
#include <desktop_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    vec2 dir = normalize(vec2(p_dirX, p_dirY) + vec2(1.0e-6, 0.0));
    // Project onto the sweep direction, normalised by the direction's L1 extent
    // so proj spans exactly [0,1] corner-to-corner for ANY direction. A plain
    // dot()+0.5 overshoots [0,1] on diagonals (a unit diagonal reaches ±0.707),
    // so the padded sweep would leave a corner sliver unwiped at t=0/t=1. ext is
    // >= 1 for a unit vector, so no division by zero. Reduces to uv.x for (1,0).
    float ext = abs(dir.x) + abs(dir.y);
    float proj = dot(uv - vec2(0.5), dir) / ext + 0.5;
    float soft = max(p_softness, 1.0e-3);
    float sweep = t * (1.0 + 2.0 * soft) - soft; // pad so it clears fully at t=0 and t=1
    // proj < sweep -> already wiped to the incoming desktop.
    float fromSide = smoothstep(sweep - soft, sweep + soft, proj);
    return mix(getToColor(uv), getFromColor(uv), fromSide);
#else
    return vec4(0.0);
#endif
}
