// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared two-texture sampler pair for full-screen virtual-desktop switch
// transitions. The kwin-effect's screen-level desktop-transition pass captures
// the OUTGOING desktop into uFromDesktop and the INCOMING desktop into
// uToDesktop, then draws a full-screen quad running the pack's pTransition over
// forward switch progress (bound as iTime). getFromColor / getToColor sample at
// a full-screen uv in [0,1]; orientation is normalised by the capture + quad on
// the effect side, so shaders sample uv directly (the GL-Transitions
// convention) and port unchanged.
//
// Desktop transitions only ever run in the kwin-effect. The samplers live in
// the PLASMAZONES_KWIN branch only, mirroring old_content.glsl's uOldWindow:
// KWin's GLShader binds them by uniform location + glActiveTexture, so no
// layout(binding) qualifier is needed. The daemon UBO target compiles through a
// strict SPIR-V path that rejects binding-less samplers, so this whole unit
// compiles away there — a desktop pack's frag must keep its getFromColor /
// getToColor calls inside its own PLASMAZONES_KWIN guard (see desktop-fade).
// Include AFTER the animation uniform block.
#ifndef PLASMAZONES_DESKTOP_TRANSITION_GLSL
#define PLASMAZONES_DESKTOP_TRANSITION_GLSL

#ifdef PLASMAZONES_KWIN
uniform sampler2D uFromDesktop;
uniform sampler2D uToDesktop;

// The captured desktop FBOs are KWin Y-up (origin bottom-left), while the
// full-screen quad hands us a top-down uv, so flip Y on the sample — same
// convention as surfaceColor / oldColor in the per-window path.
vec4 getFromColor(vec2 uv) {
    return texture(uFromDesktop, vec2(uv.x, 1.0 - uv.y));
}

vec4 getToColor(vec2 uv) {
    return texture(uToDesktop, vec2(uv.x, 1.0 - uv.y));
}

// Crossfade the two captured desktops at one uv: the outgoing desktop at t=0,
// the incoming one at t=1. The plain building block for GL-Transitions-style
// desktop packs; each pack applied its own inline mix / per-pack helper before
// this was lifted here.
vec4 crossFade(vec2 uv, float t) {
    return mix(getFromColor(uv), getToColor(uv), t);
}
#endif // PLASMAZONES_KWIN

#endif // PLASMAZONES_DESKTOP_TRANSITION_GLSL
