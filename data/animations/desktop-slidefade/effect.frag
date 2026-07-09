// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Slide & Fade — the outgoing desktop slides a short way toward its
// exit while the incoming desktop slides in from the opposite side, and the two
// crossfade across the switch. Modelled on Hyprland's "slidefade" workspace
// style. p_dirX / p_dirY set the slide axis, p_travel the slide distance (a
// fraction of the screen). `t` is forward switch progress in [0,1].
#include <desktop_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    vec2 off = sign(vec2(p_dirX, p_dirY)) * p_travel;
    vec4 fromC = getFromColor(uv + t * off);         // slides toward its exit
    vec4 toC = getToColor(uv - (1.0 - t) * off);     // slides in from the far side
    return mix(fromC, toC, smoothstep(0.0, 1.0, t)); // crossfade over the slide
#else
    return vec4(0.0);
#endif
}
