// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared one-scene sampler contract for the scrolling strip's view pass
// (`scrolling.view`, appliesTo ["strip"]). While the per-output view spring is
// in flight, the kwin-effect's StripTransitionManager renders the ordinary
// scene — columns already translated by the spring's offset, parked columns
// relocated, the tab-indicator surface riding along — into uStrip, then draws
// one full-screen quad running the pack's pTransition over it. The pack
// DECORATES the motion (blur, smear, warp); it does not own it. The scene
// keeps scrolling underneath exactly as it does with no pack assigned.
//
// THIS PASS HAS NO PROGRESS. Wheel scrolling retargets the spring on every
// batch (PreserveVelocity), so there is no discrete from/to leg and no t that
// sweeps 0 -> 1: iTime on this pass is SECONDS since the pass activated on
// this output, monotonic, never rewinding on a retarget. A pack keys its
// visible intensity off iStripMotion instead, which converges to zero as the
// spring settles.
//
// THE IDENTITY CONTRACT. A pack MUST converge to the identity image
// (getStripColor(uv) unmodified) as iStripMotion -> 0. The pass simply stops
// on the settle frame — no fade is run for you — so a pack that still
// distorts at zero motion pops when the normal scene paint takes over.
//
// The pass covers the FULL OUTPUT, but the capture holds only the STRIP
// LAYER and what lies beneath it (the desktop background, keep-below
// windows). Everything stacked above the strip — OSDs, notifications,
// floating windows, panels, daemon overlays — is excluded from the capture
// and composited sharp on top after the pass, so a pack cannot smear a
// surface that is not scrolling. stripMask() below is still useful for
// confining distortion to the strip's work area (keeping the wallpaper
// margins steady, feathering the blur at the strip's edges).
//
// Strip transitions only ever run in the kwin-effect, and compositor-only
// packs are excluded from the daemon's SPIR-V bake entirely, so the sampler
// is declared unguarded: KWin's GLShader binds it by uniform location +
// glActiveTexture, so no layout(binding) qualifier is needed.
//
// WHAT IS ACTUALLY BOUND ON THIS PASS. StripTransitionManager caches and
// pushes exactly: uStrip, iTime, iResolution, iFrame, iStripMotion,
// iStripRect, plus the customParams / customColors pools behind p_<id>.
// NOTE iResolution carries DEVICE pixels on this pass (the manager uploads
// viewport.deviceSize()), unlike the per-window path where it is logical —
// same convention as the desktop transitions. EVERY other uniform the
// animation contract declares stays at the GL default of zero here. The same
// two hazards the desktop pass documents apply, because both COMPILE cleanly
// and then render wrong:
//   - iIsReversed is never bound, so `p_reversed` is permanently false and
//     `legProgress()` returns raw iTime — which on this pass is seconds, not
//     progress, so legProgress() is meaningless here. Direction comes from
//     the SIGN of iStripMotion, never from the reversed flag.
//   - surfaceColor() is a per-WINDOW helper. It is in scope (the entry
//     prologue always includes animation_uniforms.glsl) and compiles here,
//     but returns black: iWindowOpacity and iAnchorRectInTexture are both
//     zero. oldColor() lives in shared/old_content.glsl, which no strip pack
//     includes, so reaching for it is a COMPILE error — the manager caches a
//     null-shader sentinel, abandons the pass (the plain translation shows),
//     warns on the journal once, and never recompiles that pack this
//     session. Use getStripColor() instead.
// Include AFTER the animation uniform block.
#ifndef PLASMAZONES_STRIP_TRANSITION_GLSL
#define PLASMAZONES_STRIP_TRANSITION_GLSL

uniform sampler2D uStrip;

// vec4 iStripMotion — the spring's live displacement and speed, pushed every
// frame by the kwin-effect:
//   .x  residual view offset in DEVICE px, signed. The strip is currently
//       drawn displaced this far along +x from its committed rest position;
//       it decays to 0 as the spring settles. Its SIGN is the direction the
//       strip content is travelling from, so -sign(.x) points where the
//       content is heading.
//   .y  view velocity in DEVICE px per second, signed (the smoothed rate of
//       change of .x).
//   .z  = .x / iResolution.x (offset as a fraction of the output width).
//   .w  = .y / iResolution.x (velocity in output-widths per second).
// All four converge to 0 at settle — the identity contract above keys off
// exactly this.
uniform vec4 iStripMotion;

// vec4 iStripRect — the strip's work area (the output minus panels/docks) in
// OUTPUT-LOCAL DEVICE px: (x, y, width, height), y-down from the output's
// top-left, same orientation as the uv pTransition receives. Zero-sized when
// the work area could not be resolved, in which case stripMask() below
// returns 1.0 everywhere (mask nothing rather than everything).
uniform vec4 iStripRect;

// The captured scene FBO is KWin Y-up (origin bottom-left), while the
// full-screen quad hands us a top-down uv, so flip Y on the sample — same
// convention as getFromColor/getToColor on the desktop pass.
vec4 getStripColor(vec2 uv) {
    return texture(uStrip, vec2(uv.x, 1.0 - uv.y));
}

// 1.0 inside the strip work area, 0.0 outside, with a soft edge of
// `feather` uv units (of the shorter output axis) so a masked effect fades
// at the strip boundary instead of cutting. Pass 0.0 for a hard edge. A
// zero-sized iStripRect disables masking entirely (returns 1.0).
float stripMask(vec2 uv, float feather) {
    if (iStripRect.z <= 0.0 || iStripRect.w <= 0.0) {
        return 1.0;
    }
    vec2 px = uv * iResolution;
    vec2 lo = iStripRect.xy;
    vec2 hi = iStripRect.xy + iStripRect.zw;
    float f = max(feather * min(iResolution.x, iResolution.y), 1.0e-4);
    float inX = smoothstep(lo.x - f, lo.x + f, px.x) * (1.0 - smoothstep(hi.x - f, hi.x + f, px.x));
    float inY = smoothstep(lo.y - f, lo.y + f, px.y) * (1.0 - smoothstep(hi.y - f, hi.y + f, px.y));
    return inX * inY;
}

// 1.0 well inside the output, falling to 0.0 at the LEFT and RIGHT screen
// edges over `span` uv units. Content beyond the output edge does not exist
// in the capture: uStrip is CLAMP_TO_EDGE, so a sample past the edge repeats
// the last pixel column and any displaced sample there smears/stretches the
// edge. Every pack that displaces its sample horizontally MUST scale the
// displacement by this, with `span` at least twice its maximum displacement,
// so the displacement dies out before its sample can cross the edge.
float stripEdgeFade(vec2 uv, float span) {
    float s = max(span, 1.0e-4);
    return smoothstep(0.0, s, uv.x) * (1.0 - smoothstep(1.0 - s, 1.0, uv.x));
}

// The uv remapped into the strip work area: (0,0) at the rect's top-left,
// (1,1) at its bottom-right. Meaningful only where stripMask() > 0; with a
// zero-sized rect this returns uv unchanged.
vec2 stripUv(vec2 uv) {
    if (iStripRect.z <= 0.0 || iStripRect.w <= 0.0) {
        return uv;
    }
    return (uv * iResolution - iStripRect.xy) / iStripRect.zw;
}

#endif // PLASMAZONES_STRIP_TRANSITION_GLSL
