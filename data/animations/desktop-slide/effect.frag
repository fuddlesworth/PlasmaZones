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
    // Clamp the timeline. iTime is NOT bounded to [0,1] (an overshooting curve
    // delivers its overshoot), and the fract() wrap below is only a correct
    // reveal while t stays in range: at t = 1.2 EVERY fragment fails the bounds
    // test and the whole screen shows the incoming desktop displaced 20% with a
    // hard seam; at t < 0 a column of its right edge appears before the switch
    // even starts. A slide has no third desktop to reveal, so there is nothing
    // to overshoot INTO — refusing the overshoot is the honest behaviour.
    vec2 p = uv + clamp(t, 0.0, 1.0) * s;
    vec2 f = fract(p);
    // In-bounds fragments still belong to the outgoing desktop; wrapped ones
    // reveal the incoming desktop sliding in behind.
    float fromSide = step(0.0, p.x) * step(p.x, 1.0) * step(0.0, p.y) * step(p.y, 1.0);
    return mix(getToColor(f), getFromColor(f), fromSide);
#else
    return vec4(0.0);
#endif
}
