// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rippled-glass pack, main pass: an INTERIOR-refracting pane over the blurred
// backdrop (iChannel6). Where the Glass pack bends the backdrop only along an
// edge bevel, this pack warps it across the WHOLE pane: a two-octave value-
// noise height field models the uneven glass surface, its gradient displaces
// the backdrop sample (gradient refraction — light bends toward the slope),
// and the same gradient feeds a soft directional highlight along the ripple
// crests. Chromatic fringing splits the red/blue samples along the
// displacement, matching the Glass pack's convention (p_fringing scaled by
// 0.3). The field drifts on iTime at p_rippleSpeed; 0 freezes it, giving the
// static bathroom-window look.
//
// SHARED BACKDROP STAGES, in order: the displaced sample runs through
// surfaceBackdropGrade (brightness, contrast, OKLab saturation, vibrancy),
// then the tint, and a driver-stable grain goes on last. The Edge mirror
// switch decides what a displacement reaching past the pane reads: folded
// back inside when on, the captured scene beside the pane when off.
//
// Retired handlesOpacity contract: uSurfaceOpacity is a constant 1.0 now
// (SetOpacity is layer-backed and custom chains own their alpha). The pack's
// own contentOpacity parameter fades the window content so the rippled
// backdrop shows on opaque windows; a theme's own translucent pixels reveal
// it the same way with no parameter involved.
// NO-BACKDROP FALLBACK: when the host bound nothing behind the surface
// (uHasBackdrop = 0), the pane degrades to a faint tint slab with the same
// corner rounding. The slab carries a visibility floor, so the shape still
// reads at tintStrength 0, which means no tint rather than no pane.

#include <surface_multipass.glsl>
#include <surface_noise.glsl>
#include <surface_color.glsl>

// The glass surface's height at ripple-space q: a dominant swell plus a
// finer counter-drifting octave, so the warp reads as organic ripples
// rather than a repeating wobble.
float rippleHeight(vec2 q, float t) {
    float swell = vnoise(q + vec2(t * 0.31, t * 0.23));
    float detail = vnoise(q * 2.7 + vec2(11.3, 7.1) - vec2(t * 0.17, t * 0.29));
    return swell * 0.65 + detail * 0.35;
}

// Where a bent sample lands: clamped to the canvas, or folded back inside the
// frame when the pack's Edge mirror switch is on.
vec2 rippleCoord(vec2 c) {
    return surfaceBendUv(c, p_edgeMirror >= 0.5);
}

vec4 pSurface(vec2 uv) {
    float cornerPx = p_cornerRadius * uSurfaceScale;
    SurfaceSlab slab = surfaceSlabOpen(uv, cornerPx, p_roundBottomCorners >= 0.5 ? cornerPx : 0.0, p_edgeSoftness);
    // Fade the window content over the pane; the translucency it frees is
    // filled by the rippled backdrop in slabComposite below.
    slab.window *= clamp(p_contentOpacity, 0.0, 1.0);
    vec2 px = slab.px;
    float mask = slab.mask;

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Ripple-space coordinate: p_rippleSize is the ripple's logical-px
        // size, so the pattern is DPI-stable and does not stretch with the
        // pane (unlike a frame-normalized uv).
        // The 1.0 is NOT the parameter's floor: rippleSize declares a minimum
        // of 12. It guards a hand-edited metadata.json only, keeping a zero or
        // negative out of the divide below. Same shape as the guards in the
        // sibling packs.
        float sizePx = max(p_rippleSize, 1.0) * max(uSurfaceScale, 0.001);
        // Anchored to the frame, not the canvas, so the pattern stays put when
        // a padding-declaring pack joins or leaves the chain and moves the
        // canvas origin under it. Mosaic and rain-glass anchor the same way.
        vec2 q = (px - uSurfaceFrameTopLeft) / sizePx;
        float t = iTime * max(p_rippleSpeed, 0.0);

        // Central-difference gradient of the height field.
        //
        // THE EPSILON IS A LOW-PASS, and the reason written here before ("a
        // fixed fraction of a ripple so the slope estimate stays smooth at any
        // ripple size") was not the real one. Smoothness is not at stake:
        // vnoise uses the quintic interpolant and is C2, so a far smaller
        // epsilon is exactly as smooth.
        //
        // What e = 0.35 actually does is transfer a component of frequency f at
        // sinc(0.7f). The dominant swell (f = 1) comes through at +0.368, but
        // the detail octave (vnoise(q * 2.7)) comes through at -0.057, so it is
        // attenuated 6.4x AND sign-inverted: the finer ripples bend the
        // backdrop the WRONG WAY, faintly. Neither consumer sees them, since
        // rippleHeight's value itself is never used and the refraction and the
        // glint both read only this gradient.
        //
        // The cost of that is four rippleHeight calls, each two vnoise, each
        // four hash13, so 32 hash13 per fragment over the whole canvas with
        // half of it buying the inverted term.
        //
        // LEFT AS IS DELIBERATELY. Shrinking e or dropping the octave changes
        // both the look and the cost, and this pack has not been rendered
        // against the change. The comment is corrected so the next reader does
        // not take the old justification as a reason to keep the value.
        const float e = 0.35;
        vec2 grad = vec2(rippleHeight(q + vec2(e, 0.0), t) - rippleHeight(q - vec2(e, 0.0), t),
                         rippleHeight(q + vec2(0.0, e), t) - rippleHeight(q - vec2(0.0, e), t))
            / (2.0 * e);

        // Gradient refraction: displace the backdrop sample UP-slope (the
        // gradient points uphill) by at most p_refractionStrength logical px,
        // split R/B for the fringing.
        //
        // The direction is clamped to unit length first. The raw gradient can
        // exceed 1, so multiplying it by the slider let the displacement reach
        // roughly twice the number the slider shows, and the control stopped
        // being the pixel ceiling its description promises. Below 1 the
        // gradient still scales the effect, so a gentle slope still bends less
        // than a steep one.
        vec2 dispPx = grad / max(length(grad), 1.0) * clamp(p_refractionStrength, 0.0, 40.0) * uSurfaceScale;
        vec2 shift = pxToUv(dispPx);
        float fringe = clamp(p_fringing, 0.0, 1.0) * 0.3;
        vec4 g = surfaceBlurTexel(rippleCoord(uv + shift));
        vec3 lit = g.rgb;
        // The shift has to be non-zero too, not just the fringe amount. At
        // Refraction strength 0, which is the DECLARED MINIMUM rather than an
        // exotic setting, dispPx and shift are both zero, so these two offsets
        // collapse onto uv and re-read the texel `g` already holds. Testing the
        // fringe alone let the default fringing of 0.25 pay for two dependent
        // full-canvas fetches that could not change the result.
        if (fringe > 0.001 && dot(shift, shift) > 0.0) {
            lit.r = surfaceBlurTexel(rippleCoord(uv + shift * (1.0 + fringe))).r;
            lit.b = surfaceBlurTexel(rippleCoord(uv + shift * (1.0 - fringe))).b;
        }

        // Soft directional highlight, lit where the height GRADIENT points
        // up-left. The gradient points uphill, so the lit face is the one
        // sloping AWAY from the up-left light, which is what gives the
        // ripples their raised look rather than an engraved one. Scaled by
        // how steep the ripple is, so flat glass stays clean. Premultiplied add, weighted by the backdrop alpha so
        // the glint never brightens the cleared off-capture margin.
        float slope = length(grad);
        float glint = 0.0;
        if (slope > 0.0001) {
            // px space is top-down, so up-left is negative in BOTH components.
            float facing = clamp(dot(grad / slope, vec2(-0.6, -0.8)), 0.0, 1.0);
            // Focus cue, like every other lit pack in the family: an
            // unfocused pane's own light dims to the shared 0.55 floor. This
            // glint and rain-glass's top-light were the two that ignored it,
            // so an unfocused rippled pane kept a fully lit ripple crest while
            // the glass pack beside it dimmed.
            glint = pow(facing * min(slope, 1.0), 2.0) * clamp(p_highlightStrength, 0.0, 1.0) * focusDim(0.55);
        }

        // Colour grade on the refracted sample, THEN the glint, then the tint,
        // then a driver-stable grain (the blur pack's pass order for the last
        // three).
        //
        // The glint used to be added before the grade, which is the wrong side:
        // the grade's job is to tune how the CAPTURED BACKDROP looks, and
        // running it over the pack's own specular made Brightness, Contrast and
        // Saturation double as highlight controls. rain-glass already adds its
        // top-light to the graded sample, and phosphor-glass reads its
        // excitation off the raw sample for the same reason. At the shipped
        // defaults the grade is a true identity (oklabSaturate early-returns at
        // 1.0 and the other two knobs are identity at 1), so this reorder
        // changes nothing until a user actually tunes the grade, which is the
        // case it is fixing.
        //
        // It stops HERE rather than going after the tint, where rain-glass puts
        // its highlight, because the tint's default strength is non-zero and
        // crossing it would change the shipped look.
        lit = surfaceBackdropGrade(vec4(lit, g.a), p_brightness, p_contrast, p_saturation, p_vibrancy,
                                   p_vibrancyDarkness)
                  .rgb;
        lit += glint * g.a;
        lit = mix(lit, tint * g.a, tintStrength);
        lit += surfaceGrain(px, p_noiseStrength) * g.a;
        pane = vec4(clamp(lit, 0.0, max(g.a, 0.0001)), g.a) * mask;
    } else {
        pane = faintTintSlab(tint, tintStrength, mask);
    }

    return slabComposite(slab.window, pane);
}
