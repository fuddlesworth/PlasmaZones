// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Slide — full-screen virtual-desktop push. Both desktops translate in
// the same direction; the outgoing one exits while the incoming one follows
// behind. Ported from GL-Transitions "directional" by gre
// (https://github.com/gl-transitions/gl-transitions, MIT). progress -> t;
// direction is the p_dirX / p_dirY vector.
#include <desktop_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    vec2 dir = vec2(p_dirX, p_dirY);
    vec2 p = uv + t * sign(dir);
    vec2 f = fract(p);
    // In-bounds fragments still belong to the outgoing desktop; wrapped ones
    // reveal the incoming desktop sliding in behind.
    float fromSide = step(0.0, p.x) * step(p.x, 1.0) * step(0.0, p.y) * step(p.y, 1.0);
    return mix(getToColor(f), getFromColor(f), fromSide);
#else
    return vec4(0.0);
#endif
}
