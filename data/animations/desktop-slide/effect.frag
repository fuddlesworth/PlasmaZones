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
    // Direction: follow the actual switch (iSwitchDelta via switchDirection)
    // when p_followSwitch is on, so switching left slides left and switching
    // down slides down; the configured p_dirX / p_dirY vector is the fallback
    // when no grid delta resolved, and the forced direction when the toggle is
    // off. Push both desktops along the sign of that vector. Guard the
    // degenerate all-zero case (both sliders at 0) so the switch still moves
    // instead of holding the outgoing desktop and cutting; a zero direction
    // falls back to a horizontal push. Non-zero directions (including pure
    // vertical) are unaffected — only sign() of an exact zero is redirected.
    vec2 cfg = vec2(p_dirX, p_dirY);
    vec2 s = sign((p_followSwitch > 0.5) ? switchDirection(cfg) : cfg);
    if (s == vec2(0.0)) {
        s = vec2(1.0, 0.0);
    }
    vec2 p = uv + t * s;
    vec2 f = fract(p);
    // In-bounds fragments still belong to the outgoing desktop; wrapped ones
    // reveal the incoming desktop sliding in behind.
    float fromSide = step(0.0, p.x) * step(p.x, 1.0) * step(0.0, p.y) * step(p.y, 1.0);
    return mix(getToColor(f), getFromColor(f), fromSide);
#else
    return vec4(0.0);
#endif
}
