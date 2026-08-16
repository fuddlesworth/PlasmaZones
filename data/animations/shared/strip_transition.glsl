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
// iStripAxis, iStripRect, plus the customParams / customColors pools behind
// p_<id>.
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
//     zero.
//   - surfaceSeed() (shared/noise.glsl, which a pack may well include for its
//     own noise) derives its value from iAnchorRectInTexture, so it is a
//     constant 0.0 on every fragment of every strip pass. A pack that varies
//     anything per-surface through it gets no variation at all. Seed off
//     iTime or the uv instead.
//   oldColor() lives in shared/old_content.glsl, which no strip pack
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
//   .x  residual view offset in DEVICE px, signed, measured ALONG iStripAxis.
//       The strip is currently drawn displaced this far along +iStripAxis
//       from its committed rest position — +x on a horizontal strip, +y
//       (downward, matching this pass's y-down uv) on a vertical one. It
//       decays to 0 as the spring settles. Its SIGN is the direction the
//       strip content is travelling from, so -sign(.x) * iStripAxis is the 2D
//       direction the content is heading. None of the bundled packs read
//       .x or .z: they
//       key off velocity alone. The offset lanes are part of the contract
//       and are uploaded every frame, but they ship unexercised, so treat
//       them as the less-travelled path when something looks wrong.
//   .y  view velocity in DEVICE px per second, signed (the smoothed rate of
//       change of .x, with the per-batch commit jump compensated out so a
//       new wheel batch does not spike or invert the sign for one frame).
//   .z  = .x divided by the output's DEVICE extent ALONG iStripAxis, so the
//       offset as a fraction of the output measured the way the strip
//       travels: its width on a horizontal strip, its height on a vertical
//       one. The manager divides by that extent and not by iResolution.x, so
//       a displacement tuned on one orientation is not rescaled by the aspect
//       ratio on the other.
//   .w  = .y divided by the same extent, so the velocity in output-extents
//       along the travel axis per second.
// All four converge to 0 at settle — the identity contract above keys off
// exactly this.
uniform vec4 iStripMotion;

// vec4 iStripRect — the strip's work area (the output minus panels/docks) in
// OUTPUT-LOCAL DEVICE px: (x, y, width, height), y-down from the output's
// top-left, same orientation as the uv pTransition receives. Zero-sized when
// the work area could not be resolved, in which case stripMask() below
// returns 1.0 everywhere (mask nothing rather than everything).
uniform vec4 iStripRect;

// vec2 iStripAxis — a unit vector along the strip's own travel axis for THIS
// output: (1,0) horizontal, (0,1) vertical. Pushed every frame beside
// iStripMotion.
//
// It exists because iStripMotion's lanes are SIGNED SCALARS ALONG THE STRIP,
// not along x. On a portrait monitor the strip runs top to bottom and the
// same velocity displaces content vertically. A pack that hardcodes a
// horizontal displacement still COMPILES and still looks right on a
// horizontal strip; it simply smears the wrong way on a vertical one.
//
// Multiply displacements by this (or use stripAxisOffset below) instead of
// writing vec2(amount, 0.0). Note .z/.w of iStripMotion are normalized by the
// output's extent ALONG THIS AXIS, so they stay comparable across both.
uniform vec2 iStripAxis;

// A displacement of `amount` uv units along the strip's travel axis. This is
// the axis-correct replacement for vec2(amount, 0.0).
vec2 stripAxisOffset(float amount) {
    return iStripAxis * amount;
}

// The unit vector ACROSS the strip's travel axis: (0,1) for a horizontal
// strip, (1,0) for a vertical one. Pair it with iStripAxis to write a warp in
// along/across terms instead of x/y.
//
// This is the axis SWAP, deliberately not a rotation. vec2(-iStripAxis.y,
// iStripAxis.x) would hand a vertical strip (-1, 0) and silently mirror every
// across coordinate against the horizontal case. The swap keeps
// {iStripAxis, stripAxisPerp()} orthonormal for BOTH bound values, (1,0) with
// (0,1) and (0,1) with (1,0), so iStripAxis * along + stripAxisPerp() * across
// reconstructs the original uv exactly on either axis. It is a reflection, and
// that is harmless because iStripAxis is only ever one of those two
// axis-aligned unit vectors and nothing here depends on a consistent
// orientation of the pair.
vec2 stripAxisPerp() {
    return vec2(iStripAxis.y, iStripAxis.x);
}

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

// 1.0 well inside the output, falling to 0.0 at the two screen edges the
// strip TRAVELS TOWARD — left/right on a horizontal strip, top/bottom on a
// vertical one — over `span` uv units of the output extent along that axis
// (unlike stripMask's feather, which is measured on the shorter axis — an
// easy mismatch to carry between the two). Content beyond the output edge
// does not exist in the capture: uStrip is CLAMP_TO_EDGE, so a sample past
// the edge repeats the last pixel row/column and any displaced sample there
// smears/stretches the edge.
//
// Following the axis is what pairs this helper with stripAxisOffset: the fade
// dies out in the direction the AXIS-CORRECT displacement actually goes. It is
// therefore NOT a safety net for an unported pack. A pack that still writes
// vec2(amount, 0.0) displaces horizontally on a vertical strip while this fade
// protects the top and bottom, so the two edges its samples are heading for
// get no protection at all. Port the displacement and the fade together.
//
// PRECONDITION on `span`: at least twice the maximum displacement, and never
// more than 0.5. The 2x floor is what makes the fade a guarantee rather than a
// hope — with d(a) = maxDisp * smoothstep(0, s, a) and s >= 2*maxDisp, the
// derivative of the faded displacement peaks at maxDisp * 1.5 / s <= 0.75, so
// a - d(a) is strictly increasing and a faded sample can never reach the edge.
// Above 0.5 the two ramps OVERLAP and the product never reaches 1.0 anywhere,
// which silently attenuates the whole effect in the middle of the screen
// rather than only at its edges. All three bundled consumers sit exactly at
// the 2x floor: jelly 0.04/0.08, chromatic 0.014/0.03, motion-blur 0.035/0.07.
//
// Every pack that displaces its sample along the travel axis MUST handle that
// edge one of two ways. Either scale the displacement by this fade, with
// `span` at least twice the maximum displacement, so the displacement dies out
// before its sample can cross the edge (blur, chromatic, jelly), or detect the
// out-of-range sample and paint something deliberate there instead of the
// clamped smear (carousel fades those samples into shadow). What is not
// allowed is displacing into the clamp and showing the result.
float stripEdgeFade(vec2 uv, float span) {
    float s = max(span, 1.0e-4);
    // The coordinate along the travel axis. dot() with the unit axis picks
    // uv.x horizontally and uv.y vertically without a branch.
    float along = dot(uv, iStripAxis);
    return smoothstep(0.0, s, along) * (1.0 - smoothstep(1.0 - s, 1.0, along));
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
