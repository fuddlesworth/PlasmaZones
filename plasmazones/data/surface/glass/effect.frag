// SPDX-FileCopyrightText: kwin-effects-glass contributors
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The first copyright line is there because this file says, two paragraphs down,
// that it is a FULL PORT of kwin-effects-glass, and names that project's own
// glass.glsl, snells-glass.glsl and oklab.glsl. CLAUDE.md's rule for the
// animation and pointer trees applies just as much here: a port of an upstream
// body carries a second SPDX-FileCopyrightText, because PlasmaZones is not that
// body's copyright holder. data/surface is exempt from the GPL/LGPL
// normalisation, not from crediting a third-party work. Same shape as the niri
// honeycomb port in data/animations.
//
// Better Blur and Better Blur DX are credited in prose below but NOT with a
// copyright line, because what is taken from them is an idea rather than a body:
// the concave-lens arm is a plain uniform scale toward the pane centre, written
// here, and the bevel-normal control is named after theirs.
//
// Glass pack, main pass: a refracting pane over the blurred backdrop
// (iChannel6) — a full port of kwin-effects-glass (glass.glsl /
// snells-glass.glsl / oklab.glsl / the noise pass). Three refraction modes:
// the reference's two, switched by p_physicallyBased, plus a concave lens
// (p_concaveLens, Better Blur DX's second mode) that takes precedence:
//
//   CHEAP (default): displacement along the bevel normal, up to
//   0.4 x strength of the pane at the rim, sampling INWARD so the rim
//   magnifies. The normals come from an INFLATED-radius SDF (2x the visual
//   radius, clamped 64..128 logical px) so the lens bends broadly around
//   corners.
//
//   SNELL: a 3D glass normal is built from the smoothed SDF gradient tilted
//   by the bevel profile, the view ray is bent with refract() at
//   ior = 1 + strength, and an off-axis corner lens term pulls the
//   distortion outward toward the corners.
//
// On top of either: rim glow, optional edge lighting (the backdrop's own
// light re-added along the bevel), the 2..3 px thickness glint, a
// luminance-adaptive tint, OKLab saturation, brightness, contrast and vibrancy,
// and a procedural grain that masks banding (the reference tiles a pre-rendered
// noise texture; a hash is visually equivalent here and needs no texture slot).
//
// The reference's final colorMatrix is NOT purely output colour management: it
// is built from saturation, contrast and brightness and applied after the OKLab
// saturation step, which is the same grade this pack applies below. What our
// pipeline handles at the present layer is the output transform alone. The
// deviation worth knowing is the ORDER: this pack tints before grading, where
// the reference grades and then tints.
//
// Content dimming: the window sample is dimmed by the pack's own
// p_contentOpacity parameter, so the pane stays solid and translucency
// reveals the refracted backdrop. A theme's own transparent pixels reveal
// it the same way with no parameter involved.
// NO-BACKDROP FALLBACK: when the host bound nothing behind the surface
// (uHasBackdrop = 0), the pane degrades to a faint tint slab with the same
// corner rounding. The slab carries a visibility floor, so the shape still
// reads at tintStrength 0, which means no tint rather than no pane.

#include <surface_multipass.glsl>
#include <surface_noise.glsl>
#include <surface_color.glsl>

// Where a bent sample coordinate lands: clamped to the canvas (the reference
// behaviour, an edge pixel stretched), or mirrored back inside the frame when the
// pack's Edge mirror switch is on.
//
// IN PRACTICE ONLY THE CONCAVE MODE REACHES THE SWITCH, and it is worth knowing
// why, because the control was briefly removed for want of a reader. The cheap
// mode offsets along `inward`, the negated SDF gradient. The Snell mode offsets
// along -surfaceNormal and along refract()'s xy, which for eta < 1 is the outward
// normal times a NEGATIVE scalar, since sqrt(1 - eta^2 + eta^2*nz^2) exceeds
// eta*nz for every nz whenever eta < 1. Both are inward at every fragment, and
// this pack declares no paddingParam, so it adds no margin of its own.
//
// Inward bounds the DIRECTION, not the magnitude. The cheap mode genuinely cannot
// escape, since 0.4 * concave * strength caps its offset at 0.8 of the pane's
// short side measured inward from the rim. The Snell mode can, at extreme bevel,
// strength and edge width on a small pane, where the inward push at the rim
// exceeds the pane and lands past the OPPOSITE edge. That takes settings near
// several declared maxima at once. The concave mode widens
// frameUv past the frame by up to 0.2 of the pane on the reference (green)
// channel, or 0.26 on red with fringing at its declared maximum, which is the one case the
// clamp-or-fold choice decides.
vec2 glassCoord(vec2 c) {
    return surfaceBendUv(c, p_edgeMirror >= 0.5);
}

vec4 pSurface(vec2 uv) {
    float cornerPx = p_cornerRadius * uSurfaceScale;
    SurfaceSlab slab = surfaceSlabOpen(uv, cornerPx, surfaceBottomRadius(cornerPx, p_roundBottomCorners), p_edgeSoftness);
    // Fade the window content over the pane; the translucency it frees is
    // filled by the refracted backdrop in the composite below.
    slab.window *= clamp(p_contentOpacity, 0.0, 1.0);
    vec2 px = slab.px;
    vec2 halfSz = slab.fs.halfSize;
    vec2 pos = px - slab.fs.center; // pane-centered, top-down device px
    float minHalf = min(halfSz.x, halfSz.y);
    float radius = slab.fs.radius;
    float d = slab.fs.d;
    float mask = slab.mask;

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Bevel profile from the VISUAL-radius distance (reference glass()).
        // max() keeps the clamp's hi bound above its lo bound on a degenerate
        // (tiny) frame, where minHalf * 0.9 could fall below 0.1 and GLSL clamp
        // with min > max would collapse edgePx toward 0 (an abs(d)/edgePx that
        // is 0/0 = NaN at the frame centre).
        float edgePx = clamp(p_edgeWidth * uSurfaceScale, 0.1, max(minHalf * 0.9, 0.1));
        float edgeFactor = 1.0 - clamp(abs(d) / edgePx, 0.0, 1.0);
        float eased = smoothstep(0.0, 1.0, edgeFactor);
        // Edge curve: an exponent on the bevel ramp before the circular profile.
        // Both upstreams carry the same control (Better Blur calls it "normal
        // power", kwin-effects-glass calls it refractionNormalPow). The default
        // of 1.0 here is this pack's own pre-existing look, not either
        // upstream's default, which is not verifiable from this tree.
        // eased is ~1 at the rim and falls to 0 toward the interior, so an
        // exponent below 1 RAISES the ramp and carries the bend further
        // inward, and one above 1 lowers it and confines the bend to the rim.
        // Clamped to the range metadata.json DECLARES (0.25 .. 4.0), not to a
        // wider band of its own. The declared range is the contract: the
        // settings UI cannot produce anything outside it, so a wider clamp only
        // ever admits a hand-edited profile value, rendering a bevel the author
        // never sanctioned and the UI cannot reproduce or undo.
        eased = pow(eased, clamp(p_edgeCurve, 0.25, 4.0));
        float concave = 1.0 - sqrt(max(1.0 - eased * eased, 0.0));

        float strength = clamp(p_refractionStrength, 0.0, 2.0);
        float fringe = clamp(p_fringing, 0.0, 1.0) * 0.3;
        // Refraction SDF radius: 2x visual, clamped 64..128 logical px —
        // the broadly-curved normal field that wraps the lens around
        // corners (reference glass() line: clamp(cornerRadius * 2, ...)).
        float rr = clamp(radius * 2.0, min(64.0 * uSurfaceScale, minHalf), min(128.0 * uSurfaceScale, minHalf));

        vec3 lit;
        if (p_concaveLens >= 0.5) {
            // ── Concave lens (Better Blur DX's second refraction mode) ────
            // Compress a WIDER view of the backdrop into the pane, so its edges
            // show a shrunken copy of it, the way a thick concave slab reads. Per
            // channel for the fringing. 0.2 of the pane per unit of strength, so
            // 0.4 at the declared maximum of 2.0. (The reference's own ceiling is
            // the 0.2 factor.)
            //
            // A shrunken copy of the backdrop THE CANVAS HOLDS, which on most windows
            // is genuinely wider than the pane. This pack adds no padding of its own,
            // but the canvas is the window's EXPANDED geometry with the frame rect
            // inset inside it (decoration_render.cpp pushes uSurfaceFrameTopLeft as
            // that inset), and captureWindowBackdrop blits the scene over the WHOLE
            // canvas. So on any window whose decoration carries a shadow the rim
            // samples land on real surroundings and the mode does what its name says.
            //
            // A window with no margin at all — borderless, no decoration shadow — is
            // the case where the canvas IS the frame. There the rim's mapped
            // coordinate runs past what the canvas holds, and glassCoord decides:
            // Edge mirror folds it back, and off it clamps the edge texel into a flat
            // band, which is the reference behaviour that switch's description names.
            //
            // (1 + shrink), NOT (1 - shrink). Sampling inward is what MAGNIFIES:
            // output(f) = input(0.5 + f*(1 - shrink)) spreads the centre content
            // outward across the pane, which is a convex slab and the opposite of
            // this mode's name, its description and the sentence above. At the
            // declared ceiling the pane edge sampled uv 0.80 where it needs 1.20.
            // Widening it past 1 is also what makes the edge-mirror control below
            // meaningful on this pack: the sample now reaches 0.2 of the pane
            // outside the frame, where before every mode bent inward and the
            // clamp-or-fold choice could never change a pixel.
            vec2 f = frameUv(px) - 0.5;
            float shrink = 0.2 * concave * strength;
            vec2 fG = 0.5 + f * (1.0 + shrink);
            vec2 fR = 0.5 + f * (1.0 + shrink * (1.0 + fringe));
            vec2 fB = 0.5 + f * (1.0 + shrink * (1.0 - fringe));
            vec2 topLeft = uSurfaceFrameTopLeft;
            vec2 size = uSurfaceFrameSize;
            vec4 g = surfaceBlurTexel(glassCoord(surfaceUvFromPixel(topLeft + fG * size)));
            lit = g.rgb;
            if (fringe > 0.001) {
                lit.r = surfaceBlurTexel(glassCoord(surfaceUvFromPixel(topLeft + fR * size))).r;
                lit.b = surfaceBlurTexel(glassCoord(surfaceUvFromPixel(topLeft + fB * size))).b;
            }
            pane.a = g.a;
        } else if (p_physicallyBased >= 0.5) {
            // ── Snell mode (reference snells-glass.glsl) ─────────────────
            float ior = 1.0 + strength;
            float eps = min(edgePx * 0.75, max(rr * 0.6, 0.5));
            vec2 grad = vec2(sdRoundedBox(pos + vec2(eps, 0.0), halfSz, rr)
                                 - sdRoundedBox(pos - vec2(eps, 0.0), halfSz, rr),
                             sdRoundedBox(pos + vec2(0.0, eps), halfSz, rr)
                                 - sdRoundedBox(pos - vec2(0.0, eps), halfSz, rr));
            float gradLen = length(grad);
            float bevel = clamp(p_bevelIntensity, 0.0, 3.0);
            float normalHeight = concave * bevel;
            vec2 normalXY = gradLen > 0.001 ? (grad / gradLen) * normalHeight : vec2(0.0);
            vec3 glassNormal = normalize(vec3(normalXY, 1.0));

            float lensMagnitude = concave * edgePx * bevel;
            vec2 surfaceNormal = gradLen > 0.001 ? grad / gradLen : vec2(1.0, 0.0);
            vec2 normalizedPos = pos / max(uSurfaceFrameSize, vec2(1.0));
            float cornerWeight = dot(normalizedPos, normalizedPos) * clamp(p_cornerLens, 0.0, 2.0);
            // DIRECTION in px space, MAGNITUDE from normalised space. The pull
            // used to add normalizedPos itself, which is normalised per axis, so
            // it always pointed at 45 degrees whatever the pane's aspect: on a
            // 1600x900 pane the real top-right corner is 29 degrees up-right and
            // the pull was still at 45, and on 1600x400 the corner is 14 degrees.
            // The weight is right where it is (it peaks at the corners in
            // normalised space), so only the direction is replaced, and it is
            // scaled by length(normalizedPos) to keep the term's magnitude
            // exactly what it was.
            vec2 cornerDir = length(pos) > 0.001 ? normalize(pos) : vec2(0.0);
            surfaceNormal += cornerDir * length(normalizedPos) * concave * cornerWeight;
            // Scaled by strength like the refract term below it. Unscaled, a
            // Refraction strength of 0 still left this whole displacement
            // standing (about edgeWidth x bevelIntensity device px at the rim),
            // so the control could not switch the bend off the way its
            // description says it does.
            vec2 lensShift = pxToUv(-surfaceNormal * lensMagnitude * strength);

            vec3 refractG = refract(vec3(0.0, 0.0, -1.0), glassNormal, 1.0 / ior);
            vec2 dirPx = length(refractG.xy) > 0.001 ? normalize(refractG.xy) : vec2(0.0);
            float magnitude = lensMagnitude * strength;
            vec2 shiftG = pxToUv(dirPx * magnitude) + lensShift;
            vec4 g = surfaceBlurTexel(glassCoord(uv + shiftG));
            lit = g.rgb;
            if (fringe > 0.001) {
                vec2 shiftR = pxToUv(dirPx * (magnitude * (1.0 + fringe))) + lensShift;
                vec2 shiftB = pxToUv(dirPx * (magnitude * (1.0 - fringe))) + lensShift;
                lit.r = surfaceBlurTexel(glassCoord(uv + shiftR)).r;
                lit.b = surfaceBlurTexel(glassCoord(uv + shiftB)).b;
            }
            pane.a = g.a;
        } else {
            // ── Cheap mode (reference glassRefraction) ───────────────────
            const float h = 1.0;
            vec2 grad = vec2(sdRoundedBox(pos + vec2(h, 0.0), halfSz, rr) - sdRoundedBox(pos - vec2(h, 0.0), halfSz, rr),
                             sdRoundedBox(pos + vec2(0.0, h), halfSz, rr) - sdRoundedBox(pos - vec2(0.0, h), halfSz, rr));
            vec2 inward = length(grad) > 0.001 ? -normalize(grad) : vec2(0.0, 1.0);
            // No min() here: concave is at most 1 and strength at most 2, so the
            // product peaks at 0.8 and the 1.0 ceiling could never bind. It read
            // as though cheap mode might displace by a whole pane.
            float strengthUv = 0.4 * concave * strength;
            // Same px -> uv conversion the Snell branch gets from pxToUv, so
            // the two modes share one Y convention instead of hand-rolling a
            // second copy that only tracked the compositor.
            //
            // ISOTROPIC IN DEVICE px, against the pane's SHORT side. Scaling by the
            // whole uSurfaceFrameSize made the displacement a fraction of each axis
            // SEPARATELY, so on a 1600x400 pane one "Refraction strength" bent the
            // backdrop four times as far left and right as it did up and down. A
            // strength slider that means a different amount depending on which way
            // the surface faces has no coherent reading, the same objection as the
            // frost crystals inheriting the pane aspect. The short side keeps a
            // square pane byte-identical and takes the larger axis down to match.
            //
            // This does NOT close the gap between the two modes' magnitudes: cheap
            // is a fraction of the pane and Snell a fraction of the bevel width, so
            // the toggle still changes the bend by roughly an order of magnitude on
            // a large window. Both are internally coherent, unifying them would be a
            // look change with no correctness argument behind it, and the two
            // parameter descriptions now say which scale each bends against.
            float paneShortPx = min(uSurfaceFrameSize.x, uSurfaceFrameSize.y);
            vec2 dirUv = pxToUv(inward * strengthUv * paneShortPx);
            vec4 g = surfaceBlurTexel(glassCoord(uv + dirUv));
            lit = g.rgb;
            // Gated the way the concave and Snell arms already gate theirs. Run
            // unconditionally these cost two dependent fetches per fragment that
            // return the texel already in `g` for two separate reasons: at
            // fringing 0 the two offsets collapse onto dirUv, and anywhere
            // deeper into the pane than the bevel `concave` is exactly 0, so
            // strengthUv and dirUv are zero and all three fetches hit one texel.
            // That second case covers most of the pane at the shipped defaults.
            if (fringe > 0.001 && strengthUv > 0.0) {
                lit.r = surfaceBlurTexel(glassCoord(uv + dirUv * (1.0 + fringe))).r;
                lit.b = surfaceBlurTexel(glassCoord(uv + dirUv * (1.0 - fringe))).b;
            }
            pane.a = g.a;
        }

        // ── Straight alpha from here to the re-premultiply at the end ────
        //
        // Everything below combines `lit` with STRAIGHT quantities: the rim
        // mixes toward p_rimColor.rgb, the glint mixes toward vec3(1.0), edge
        // lighting scales the sample outright, and the tint mixes toward
        // `tint`. The capture is PREMULTIPLIED, so doing any of that against a
        // premultiplied value produced rgb > a wherever the pane was not
        // opaque, which is the invariant every composite downstream relies on.
        // Edge lighting was the worst of them (`glow += lit * concave` doubles
        // the sample at the rim), the grain was simply added afterwards with no
        // bound at all, and surfaceColorAdjust clamps to 1.0 rather than to
        // alpha so it could not rescue any of it.
        //
        // Un-premultiply ONCE here, run the reference's pass order on straight
        // colour where it belongs, and re-premultiply ONCE at the end. Where
        // the pane is opaque, which is the whole interior of a window over a
        // captured backdrop, this is exactly what the code did before.
        //
        // NOT surfaceBackdropGrade, deliberately: that helper runs brightness,
        // contrast, saturation and vibrancy in one call, and glass needs its
        // OKLab saturation BETWEEN the contrast and the vibrancy to keep the
        // reference's order. Folding onto the helper would move that step.
        float paneAlpha = max(pane.a, 0.0001);
        lit = pane.a > 0.0001 ? lit / paneAlpha : vec3(0.0);

        // Rim glow + optional edge lighting (reference glassOutline).
        float dim = focusDim(0.55);
        float rimStrength = clamp(p_rimStrength, 0.0, 1.0) * dim;
        // The slider SCALES the rim rather than capping it. It used to be
        // clamp(0.25 * concave, 0.0, rimStrength), which made it a ceiling: since
        // 0.25 * concave never exceeds 0.25, any value at or above 0.25 never
        // bound at all, so 0.25 and 1.0 rendered identically and the shipped
        // default of 0.35 already sat in that dead range. Values below 0.25 had
        // the opposite fault, flattening the profile into a constant band.
        // Normalised on the default so the pack still renders as it shipped.
        const float kRimDefaultStrength = 0.35;
        float rimMask = 0.25 * concave * (rimStrength / kRimDefaultStrength);
        vec3 glow = mix(lit, p_rimColor.rgb, rimMask);
        if (p_edgeLighting >= 0.5) {
            glow += lit * concave;
        }

        // Thickness glint: a 2..3 LOGICAL px band inside the rim mixed toward
        // white at the two diagonal ends of the pane.
        //
        // Every smoothstep here is written low-edge-first. Several were
        // smoothstep(hi, lo, x), which the GLSL spec leaves UNDEFINED for
        // edge0 > edge1 even though every driver in practice evaluates it as the
        // mirror; `1.0 - smoothstep(lo, hi, x)` is the defined spelling of the
        // same curve.
        if (rimStrength > 0.0) {
            // Scaled like every other length in this pack. In raw device px the
            // band stayed 2..3 device px, so it thinned to a hairline at 200%.
            float bandPx = max(uSurfaceScale, 0.001);
            float edgeMask = 1.0 - smoothstep(-2.0 * bandPx, 0.0, d);
            float borderInner = 1.0 - smoothstep(-3.0 * bandPx, -1.0 * bandPx, d);
            float edgeProfile = pow(max(edgeMask - borderInner, 0.0), 0.9);
            // Bail where the profile is provably zero, which is most of the
            // pane. edgeMask saturates to 1 at d <= -2 band-px and borderInner
            // at d <= -3, so edgeProfile is EXACTLY 0 everywhere deeper than
            // three band-px inside the edge. On a 1600x900 pane that band is
            // about 15,000 of 1,440,000 fragments, and the four smoothsteps and
            // two mixes below were all being multiplied by that zero.
            if (edgeProfile > 0.0) {
            // Two DIAGONAL position weights, not a shadow and a highlight: both
            // mix toward white, one peaking at the top-left of the pane and one
            // at the bottom-right. The names said otherwise and nothing here
            // darkens anything.
            float glintTopLeft = (1.0 - smoothstep(-halfSz.y * 1.4, halfSz.y * 1.4, pos.y))
                * (1.0 - smoothstep(-halfSz.x * 1.4, halfSz.x * 1.4, pos.x));
            float glintBottomRight = smoothstep(-halfSz.y * 1.4, halfSz.y * 1.4, pos.y)
                * smoothstep(-halfSz.x * 1.4, halfSz.x * 1.4, pos.x);
            // The glint is part of the rim treatment, so it follows the Rim light
            // slider and the focus dim (both already folded into rimStrength).
            // It used to be GATED on that slider and then ignore it, so a rim of
            // 0.01 still produced a full-strength glint and an unfocused window
            // kept a glint its rim had lost. Normalised on the default exactly as
            // rimMask above is, so the shipped look is unchanged, and clamped
            // because a mix weight past 1 would overshoot white and break the
            // premultiplied invariant.
            float glintScale = rimStrength / kRimDefaultStrength;
            glow = mix(glow, vec3(1.0), clamp(edgeProfile * glintTopLeft * glintScale, 0.0, 1.0));
            glow = mix(glow, vec3(1.0), clamp(edgeProfile * glintBottomRight * glintScale, 0.0, 1.0));
            }
        }
        // Unconditionally: `concave < 1.0 ? glow : lit` was inert in the taken
        // arm and destructive in the other. concave reaches 1.0 only where
        // eased == 1.0, which is d == 0 exactly, and that is precisely where the
        // rim mix and the edge-lighting add are at their strongest, so the one
        // case the ternary treated specially was the one it threw away.
        lit = glow;

        // Luminance-adaptive tint (reference adjustedTintStrength), then
        // OKLab saturation, then grain — the reference's pass order.
        float tintAdj = tintStrength * clamp(abs(luma601(lit) - luma601(tint)), 0.0, 1.0);
        // `tint`, not `tint * pane.a`: both sides of the mix are straight now.
        lit = mix(lit, tint, tintAdj);
        // Brightness and contrast ahead of the reference's saturation step,
        // then vibrancy after it, both on the premultiplied value the
        // reference saturates (the backdrop under a window is effectively
        // opaque, so the premultiply is a no-op there).
        lit = surfaceColorAdjust(lit, p_brightness, p_contrast, 1.0);
        lit = oklabSaturate(lit, clamp(p_saturation, 0.0, 2.0));
        lit = surfaceVibrancy(lit, p_vibrancy, p_vibrancyDarkness);
        lit += (hashSin(px) - 0.5) * 2.0 * clamp(p_noiseStrength, 0.0, 0.2);

        // Re-premultiply once, clamped in straight space first so the result
        // satisfies rgb <= a by construction rather than by hoping the terms
        // above stayed in range. hashSin here rather than hash13 is deliberate
        // and surface_noise.glsl says why.
        pane = vec4(clamp(lit, 0.0, 1.0) * pane.a, pane.a) * mask;
    } else {
        pane = faintTintSlab(tint, tintStrength, mask);
    }

    return slabComposite(slab.window, pane);
}
