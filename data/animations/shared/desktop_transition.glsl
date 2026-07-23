// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared two-texture sampler pair for the full-screen desktop transitions: the
// virtual-desktop SWITCH (`desktop.switch`) and the show-desktop PEEK
// (`desktop.peek`). The kwin-effect's screen-level desktop-transition pass
// captures the two endpoints into uFromDesktop / uToDesktop, then draws a
// full-screen quad running the pack's pTransition over progress bound as iTime.
// getFromColor / getToColor sample at a full-screen uv in [0,1]; orientation is
// normalised by the capture + quad on the effect side, so shaders sample uv
// directly (the GL-Transitions convention) and port unchanged.
//
// What the endpoints mean, and which way iTime runs, depends on the event:
//   desktop.switch — FROM = the outgoing desktop, TO = the incoming one. iTime
//     always runs forward, 0 -> 1.
//   desktop.peek   — FROM = the windows scene, TO = the bare desktop, for BOTH
//     legs. The hide leg runs iTime forward 0 -> 1; the SHOW leg reverses time,
//     1 -> 0, over that same unswapped pair, so it is literally the hide leg
//     played backwards and an asymmetric pack retraces its own motion. Do not
//     assume iTime is monotonically increasing, and do not assume FROM is
//     whatever is currently on screen.
// A stateful/overshooting curve can drive iTime slightly outside [0,1] on
// either event (the show leg mirrors an overshoot past 1 into negative t), so a
// pack must hold its endpoints for a t just outside the range. Clamping t up
// front is the simplest way and most packs do it. Feeding t through a padded
// smoothstep works too, since smoothstep saturates on its own — desktop-wipe,
// desktop-circle, desktop-dissolve, desktop-aretha, desktop-phosphor and
// desktop-fade take that route and need no clamp of their own. Either way is
// fine; what is NOT fine is a pack that only holds its endpoints for t
// exactly 0 and 1.
//
// Desktop transitions only ever run in the kwin-effect, and compositor-only
// packs are excluded from the daemon's SPIR-V bake entirely, so the samplers
// are declared unguarded: KWin's GLShader binds them by uniform location +
// glActiveTexture, so no layout(binding) qualifier is needed.
//
// WHAT IS ACTUALLY BOUND ON THIS PASS. DesktopTransitionManager caches and
// pushes exactly: uFromDesktop, uToDesktop, iTime, iResolution, iFrame,
// iSwitchDelta, plus the customParams / customColors pools behind p_<id>.
// NOTE iResolution carries DEVICE pixels on this pass (the manager uploads
// viewport.deviceSize()), unlike the per-window path where it is logical.
// Aspect ratios are unaffected; anything deriving a PIXEL-scale feature size
// from it renders that feature at device scale on a scaled output.
// EVERY other uniform the animation contract declares stays at the GL
// default of zero here. Two consequences worth stating, because both
// COMPILE cleanly and then render wrong:
//   - iIsReversed is never bound, so `p_reversed` is permanently false and
//     `legProgress()` returns raw iTime. Direction on this pass comes ONLY
//     from the iTime sweep (the peek SHOW leg runs time backwards — see the
//     endpoint note above), never from the reversed flag.
//   - surfaceColor() is a per-WINDOW helper. It is in scope (the entry
//     prologue always includes animation_uniforms.glsl) and compiles here,
//     but returns black: iWindowOpacity and iAnchorRectInTexture are both
//     zero. oldColor() is a different hazard — it lives in
//     shared/old_content.glsl, which no desktop pack includes, so reaching
//     for it is a COMPILE error. On THIS pass that does not look like the
//     daemon's flat-gray swallow: the manager caches a null-shader sentinel
//     and ABANDONS the transition, so you get an instant desktop cut, a
//     warning on the journal, and no recompile for the rest of the session.
//     Use getFromColor() / getToColor() instead.
// Include AFTER the animation uniform block.
#ifndef PLASMAZONES_DESKTOP_TRANSITION_GLSL
#define PLASMAZONES_DESKTOP_TRANSITION_GLSL

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
// space pTransition receives. Only .zw is consumed here; the .xy cell delta
// is contract surface for packs that scale travel by the switch DISTANCE
// (e.g. a two-desktop jump travelling twice as far) — no bundled pack reads
// it yet.
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

// Crossfade the two captured desktops at one uv: the outgoing desktop at t=0,
// the incoming one at t=1. The plain building block for GL-Transitions-style
// desktop packs; each pack applied its own inline mix / per-pack helper before
// this was lifted here.
vec4 crossFade(vec2 uv, float t) {
    return mix(getFromColor(uv), getToColor(uv), t);
}

#endif // PLASMAZONES_DESKTOP_TRANSITION_GLSL
