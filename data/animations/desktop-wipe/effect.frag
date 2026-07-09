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
    float proj = dot(uv - vec2(0.5), dir) + 0.5; // 0..1 along the sweep direction
    float soft = max(p_softness, 1.0e-3);
    float sweep = t * (1.0 + 2.0 * soft) - soft; // pad so it clears fully at t=0 and t=1
    // proj < sweep -> already wiped to the incoming desktop.
    float fromSide = smoothstep(sweep - soft, sweep + soft, proj);
    return mix(getToColor(uv), getFromColor(uv), fromSide);
#else
    return vec4(0.0);
#endif
}
