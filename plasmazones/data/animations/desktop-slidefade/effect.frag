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
    // Slide axis follows the actual switch direction when p_followSwitch is
    // on; the configured p_dirX / p_dirY vector is the fallback / override.
    vec2 cfg = vec2(p_dirX, p_dirY);
    vec2 off = sign((p_followSwitch > 0.5) ? switchDirection(cfg) : cfg) * p_travel;
    // Clamp the timeline. iTime is NOT bounded to [0,1] — an overshooting curve
    // delivers its overshoot — and the two sample offsets would then displace a
    // desktop that has already fully arrived: at t = 1.25 the crossfade has
    // saturated to `toC`, which is sampled at -0.25 * travel and smears a band of
    // clamp-to-edge pixels across the screen. The mirror happens below 0. Like its
    // sibling desktop-slide, a slide has no third desktop to overshoot into.
    // (The mix factor is safe on its own — GLSL smoothstep clamps internally.)
    float ts = clamp(t, 0.0, 1.0);
    vec4 fromC = getFromColor(uv + ts * off);         // slides toward its exit
    vec4 toC = getToColor(uv - (1.0 - ts) * off);     // slides in from the far side
    return mix(fromC, toC, smoothstep(0.0, 1.0, ts)); // crossfade over the slide
}
