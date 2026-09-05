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
// The pass covers the FULL OUTPUT, but getStripColor() returns ONLY THE
// STRIP LAYER: the columns, the tab pills, and nothing else. The pass
// renders the scene into uStrip with everything stacked BELOW the strip
// (the desktop background, keep-below windows) painted first, so the
// compositor's blur and translucency have their backdrop; at the strip
// band's bottom edge it copies that below-strip content into uBelow and
// zeroes uStrip's alpha, then the columns paint over it with their real
// coverage. getStripColor() subtracts uBelow back out of every sample, and
// the entry point (PZ_FINALIZE_COLOR below) re-composites the pack's
// output over the UNDISPLACED uBelow. Everything stacked above the strip
// (OSDs, notifications, floating windows, panels, daemon overlays) is
// excluded from the capture and composited sharp on top after the pass. So
// a pack cannot smear a surface that is not scrolling, and the wallpaper
// stays still no matter how the pack warps the strip. The software cursor
// is kept out the same way: the compositor's cursor is hidden for the pass
// and the pass blits it itself as its last draw.
//
// getStripColor() CARRIES REAL ALPHA: premultiplied pixels with a = 1 over
// an opaque column, a = 0 in the gaps between columns and everywhere no
// column reaches, fractional along anti-aliased corners, shadows and
// translucent clients. The re-composite is premultiplied (out = pack +
// below * (1 - pack.a)), so a pack's result must stay premultiplied: rgb <=
// a everywhere, additive light scaled by the coverage it decorates, and
// alpha carried through from the samples rather than forced to 1. Pure
// resamples and averages of getStripColor() (blur, warp, bow) keep the
// invariant by construction. A pack that takes channels from different taps
// or adds its own light must keep it deliberately (see strip-chromatic and
// phosphor-gate). stripMask() below is still useful for confining
// distortion to the strip's work area (feathering the blur at the strip's
// edges).
//
// Strip transitions only ever ATTACH in the kwin-effect (the daemon has no
// strip scene to capture), but the pack sources compile on both uniform
// ABIs so the settings preview can play them against a stand-in scene and
// the SPIR-V bake tests cover them. On the kwin branch the sampler is
// declared unguarded — KWin's GLShader binds it by uniform location +
// glActiveTexture, so no layout(binding) qualifier is needed. On the UBO
// branch it aliases user-texture slot 1 (uStrip = uTexture1, fed via
// ShaderEffect::setUserTexture) and the iStrip* scalars come from the
// AnimationUniforms block's transition tail.
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
//     own noise) derives its value from iSurfaceScreenPos, so it is a
//     constant 0.0 on every fragment of every strip pass. A pack that varies
//     anything per-surface through it gets no variation at all. Seed off
//     iTime or the uv instead.
//   - iStripAxis is bound by StripTransitionManager, and it is the one uniform
//     here whose zero default is catastrophic rather than merely wrong. At
//     vec2(0) stripAxisOffset() returns vec2(0), so every displacing pack
//     renders the identity image and looks like it does nothing at all, and
//     stripEdgeFade()'s dot(uv, vec2(0)) is 0 on every fragment, so every fade
//     collapses to zero and takes the effect it scales with it. Any NEW
//     consumer of this pass (the preview renderer, the thumbnail path) must
//     bind it before it binds anything else here, or every strip pack it
//     renders comes out blank rather than merely mis-oriented.
//   - oldColor() lives in shared/old_content.glsl, which no strip pack
//     includes, so reaching for it is a COMPILE error — the manager caches a
//     null-shader sentinel, abandons the pass (the plain translation shows),
//     warns on the journal once, and never recompiles that pack this
//     session. Use getStripColor() instead.
// Include AFTER the animation uniform block.
#ifndef PLASMAZONES_STRIP_TRANSITION_GLSL
#define PLASMAZONES_STRIP_TRANSITION_GLSL

#ifdef PLASMAZONES_KWIN
uniform sampler2D uStrip;
uniform sampler2D uBelow;
#else
#define uStrip uTexture1
// Slot 2 is never fed on the UBO branch (the preview plays the pack against
// a stand-in scene that IS the strip layer), and an unfed sampler reads as
// transparent black there — which makes both uses of uBelow below the
// identity, exactly as intended.
#define uBelow uTexture2
#endif

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
#ifdef PLASMAZONES_KWIN
uniform vec4 iStripMotion;
#endif

// vec4 iStripRect — the strip's work area (the output minus panels/docks) in
// OUTPUT-LOCAL DEVICE px: (x, y, width, height), y-down from the output's
// top-left, same orientation as the uv pTransition receives. Zero-sized when
// the work area could not be resolved, in which case stripMask() below
// returns 1.0 everywhere (mask nothing rather than everything).
#ifdef PLASMAZONES_KWIN
uniform vec4 iStripRect;
#endif

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
#ifdef PLASMAZONES_KWIN
uniform vec2 iStripAxis;
#endif

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
// convention as getFromColor/getToColor on the desktop pass. A Qt-RHI
// uploaded scene is top-origin (Y-down), so the UBO branch samples the uv
// directly.
vec2 stripSampleUv(vec2 uv) {
#ifdef PLASMAZONES_KWIN
    return vec2(uv.x, 1.0 - uv.y);
#else
    return uv;
#endif
}

// The strip layer alone, premultiplied. uStrip holds the columns composited
// over the below-strip content (so blur and translucency are already
// resolved in it) with alpha = the columns' coverage; uBelow holds that
// below-strip content on its own. Over a column the capture is exactly
// window + below * (1 - a), so subtracting below * (1 - a) leaves the
// window layer: opaque content is untouched, a translucent client keeps
// only its own contribution, and a gap (a = 0) comes back as zero. The
// max() only absorbs the quantisation of an 8-bit capture, which can leave
// a channel a fraction below its exact value; the invariant rgb <= a holds
// by construction otherwise.
vec4 getStripColor(vec2 uv) {
    vec2 suv = stripSampleUv(uv);
    vec4 c = texture(uStrip, suv);
    vec4 below = texture(uBelow, suv);
    return vec4(max(c.rgb - below.rgb * (1.0 - c.a), vec3(0.0)), c.a);
}

// The strip pass's final-colour hook: put the below-strip content back,
// UNDISPLACED, under whatever the pack produced from the strip layer. This
// is what makes the wallpaper stand still while a pack warps the columns
// over it. Overrides the identity default from animation_uniforms.glsl
// (included by the prologue, so it is already defined here) for the kwin
// branch only; the UBO branch keeps the identity and its unfed uBelow. The
// output is opaque: the quad replaces the frame.
#ifdef PLASMAZONES_KWIN
vec4 stripComposite(vec4 packColor) {
    vec4 below = texture(uBelow, stripSampleUv(vTexCoord));
    return vec4(packColor.rgb + below.rgb * (1.0 - packColor.a), 1.0);
}
#undef PZ_FINALIZE_COLOR
#define PZ_FINALIZE_COLOR(c) stripComposite(c)
#endif

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
// rather than only at its edges. Jelly and motion-blur sit exactly at the 2x
// floor (0.04/0.08 and 0.035/0.07); chromatic sits just above it (0.014/0.03).
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
