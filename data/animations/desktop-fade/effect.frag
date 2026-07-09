// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Fade — full-screen virtual-desktop switch cross-fade. `t` is forward
// switch progress in [0,1]; blend the outgoing desktop into the incoming one
// with a smoothstep so the midpoint isn't a flat 50/50 wash. Run by the
// kwin-effect's screen-level desktop-transition pass, which binds uFromDesktop
// (outgoing) and uToDesktop (incoming) and pushes progress as iTime.
#include <desktop_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    float a = smoothstep(0.0, 1.0, clamp(t, 0.0, 1.0));
    return mix(getFromColor(uv), getToColor(uv), a);
#else
    // Desktop transitions are compositor-only; the daemon never runs them.
    // Return transparent so the pack still bakes for the daemon target.
    return vec4(0.0);
#endif
}
