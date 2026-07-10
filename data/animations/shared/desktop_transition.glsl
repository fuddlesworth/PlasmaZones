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

// vec4 iSwitchDelta — the actual switch direction, pushed once per transition
// by the kwin-effect (DesktopTransitionManager::begin computes it from the
// pager grid). .xy is the wrap-corrected desktop-grid delta in cells: +x = the
// switch moved one column right, +y = one row down, and a wrapping
// next-desktop jump off the last column reads as +1, not a full-row jump
// backwards. .zw is the same delta normalized to unit length. All zeros when
// the grid positions could not be resolved. Direction-aware packs read it
// through switchDirection() below so their configured direction params still
// apply as the fallback (and as the forced direction when the pack's
// followSwitch toggle is off).
uniform vec4 iSwitchDelta;

// The switch direction as a unit vector, falling back to `fallback` (a pack's
// configured direction params, passed through un-normalized) when the runtime
// supplied no usable delta. +x is right, +y is down, matching the top-down uv
// space pTransition receives.
vec2 switchDirection(vec2 fallback) {
    return dot(iSwitchDelta.zw, iSwitchDelta.zw) > 1.0e-6 ? iSwitchDelta.zw : fallback;
}

// The captured desktop FBOs are KWin Y-up (origin bottom-left), while the
// full-screen quad hands us a top-down uv, so flip Y on the sample — same
// convention as surfaceColor / oldColor in the per-window path.
vec4 getFromColor(vec2 uv) {
    return texture(uFromDesktop, vec2(uv.x, 1.0 - uv.y));
}

vec4 getToColor(vec2 uv) {
    return texture(uToDesktop, vec2(uv.x, 1.0 - uv.y));
}
#endif // PLASMAZONES_KWIN

#endif // PLASMAZONES_DESKTOP_TRANSITION_GLSL
