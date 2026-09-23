// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
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
// luminance-adaptive tint, OKLab saturation, and a procedural grain that
// masks banding (the reference tiles a pre-rendered noise texture; a hash
// is visually equivalent here and needs no texture slot). The reference's
// final colorMatrix is KWin output colour management, which our pipeline
// applies at the present/KWin layer — nothing to port shader-side.
//
// Content dimming: the window sample is dimmed by the pack's own
// p_contentOpacity parameter, so the pane stays solid and translucency
// reveals the refracted backdrop. A theme's own transparent pixels reveal
// it the same way with no parameter involved.
// NO-BACKDROP FALLBACK: when the host bound nothing behind the surface
// (uHasBackdrop = 0), the pane degrades to a faint tint slab with the same
// corner rounding.

#include <surface_multipass.glsl>
#include <surface_noise.glsl>
#include <surface_color.glsl>

// Where a bent sample coordinate lands: clamped to the canvas (the reference
// behaviour, an edge pixel stretched), or mirrored back inside the frame when
// the pack's Edge mirror switch is on.
vec2 glassCoord(vec2 c) {
    return p_edgeMirror >= 0.5 ? frameMirrorUv(c) : clamp(c, 0.0, 1.0);
}

vec4 pSurface(vec2 uv) {
    float cornerPx = p_cornerRadius * uSurfaceScale;
    SurfaceSlab slab = surfaceSlabOpen(uv, cornerPx, p_roundBottomCorners >= 0.5 ? cornerPx : 0.0, p_edgeSoftness);
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
        // Edge curve: an exponent on the bevel ramp before the circular
        // profile (Better Blur's "normal power"). 1 is the reference bevel;
        // below 1 pushes the bend out to the rim, above 1 spreads it inward.
        eased = pow(eased, clamp(p_edgeCurve, 0.05, 8.0));
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
            // Scale the whole backdrop toward the pane's centre by the bevel
            // profile, so the edges show a shrunken copy of the interior, the
            // way a thick concave slab reads. Per channel for the fringing.
            // 0.2 of the pane at full strength, the reference's ceiling.
            vec2 f = frameUv(px) - 0.5;
            float shrink = 0.2 * concave * strength;
            vec2 fG = 0.5 + f * (1.0 - shrink);
            vec2 fR = 0.5 + f * (1.0 - shrink * (1.0 + fringe));
            vec2 fB = 0.5 + f * (1.0 - shrink * (1.0 - fringe));
            vec2 topLeft = uSurfaceFrameTopLeft;
            vec2 size = uSurfaceFrameSize;
            vec4 g = texture(iChannel6, glassCoord(surfaceUvFromPixel(topLeft + fG * size)));
            lit = g.rgb;
            if (fringe > 0.001) {
                lit.r = texture(iChannel6, glassCoord(surfaceUvFromPixel(topLeft + fR * size))).r;
                lit.b = texture(iChannel6, glassCoord(surfaceUvFromPixel(topLeft + fB * size))).b;
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
            surfaceNormal += normalizedPos * concave * cornerWeight;
            vec2 lensShift = pxToUv(-surfaceNormal * lensMagnitude);

            vec3 refractG = refract(vec3(0.0, 0.0, -1.0), glassNormal, 1.0 / ior);
            vec2 dirPx = length(refractG.xy) > 0.001 ? normalize(refractG.xy) : vec2(0.0);
            float magnitude = lensMagnitude * strength;
            vec2 shiftG = pxToUv(dirPx * magnitude) + lensShift;
            vec4 g = texture(iChannel6, glassCoord(uv + shiftG));
            lit = g.rgb;
            if (fringe > 0.001) {
                vec2 shiftR = pxToUv(dirPx * (magnitude * (1.0 + fringe))) + lensShift;
                vec2 shiftB = pxToUv(dirPx * (magnitude * (1.0 - fringe))) + lensShift;
                lit.r = texture(iChannel6, glassCoord(uv + shiftR)).r;
                lit.b = texture(iChannel6, glassCoord(uv + shiftB)).b;
            }
            pane.a = g.a;
        } else {
            // ── Cheap mode (reference glassRefraction) ───────────────────
            const float h = 1.0;
            vec2 grad = vec2(sdRoundedBox(pos + vec2(h, 0.0), halfSz, rr) - sdRoundedBox(pos - vec2(h, 0.0), halfSz, rr),
                             sdRoundedBox(pos + vec2(0.0, h), halfSz, rr) - sdRoundedBox(pos - vec2(0.0, h), halfSz, rr));
            vec2 inward = length(grad) > 0.001 ? -normalize(grad) : vec2(0.0, 1.0);
            float strengthUv = min(0.4 * concave * strength, 1.0);
            // Same px -> uv conversion the Snell branch gets from pxToUv, so
            // the two modes share one Y convention instead of hand-rolling a
            // second copy that only tracked the compositor. The frame/canvas
            // ratio stays: this offset is expressed relative to the frame.
            vec2 dirUv = pxToUv(inward * strengthUv * uSurfaceFrameSize);
            vec4 g = texture(iChannel6, glassCoord(uv + dirUv));
            lit = vec3(texture(iChannel6, glassCoord(uv + dirUv * (1.0 + fringe))).r, g.g,
                       texture(iChannel6, glassCoord(uv + dirUv * (1.0 - fringe))).b);
            pane.a = g.a;
        }

        // Rim glow + optional edge lighting (reference glassOutline).
        float dim = focusDim(0.55);
        float rimStrength = clamp(p_rimStrength, 0.0, 1.0) * dim;
        float rimMask = clamp(0.25 * concave, 0.0, rimStrength);
        vec3 glow = mix(lit, p_rimColor.rgb, rimMask);
        if (p_edgeLighting >= 0.5) {
            glow += lit * concave;
        }

        // Thickness glint: a 2..3 px band inside the rim mixed toward
        // white, weighted by position across the pane.
        if (rimStrength > 0.0) {
            float edgeMask = smoothstep(0.0, -2.0, d);
            float borderInner = smoothstep(-1.0, -3.0, d);
            float edgeProfile = pow(max(edgeMask - borderInner, 0.0), 0.9);
            float shadowMask = smoothstep(halfSz.y * 1.4, -halfSz.y * 1.4, pos.y)
                * smoothstep(halfSz.x * 1.4, -halfSz.x * 1.4, pos.x);
            float highlightMask = smoothstep(-halfSz.y * 1.4, halfSz.y * 1.4, pos.y)
                * smoothstep(-halfSz.x * 1.4, halfSz.x * 1.4, pos.x);
            glow = mix(glow, vec3(1.0), edgeProfile * shadowMask);
            glow = mix(glow, vec3(1.0), edgeProfile * highlightMask);
        }
        lit = concave < 1.0 ? glow : lit;

        // Luminance-adaptive tint (reference adjustedTintStrength), then
        // OKLab saturation, then grain — the reference's pass order.
        float tintAdj = tintStrength * clamp(abs(luma601(lit) - luma601(tint)), 0.0, 1.0);
        lit = mix(lit, tint * pane.a, tintAdj);
        // Brightness and contrast ahead of the reference's saturation step,
        // then vibrancy after it, both on the premultiplied value the
        // reference saturates (the backdrop under a window is effectively
        // opaque, so the premultiply is a no-op there).
        lit = surfaceColorAdjust(lit, p_brightness, p_contrast, 1.0);
        lit = oklabSaturate(lit, clamp(p_saturation, 0.0, 2.0));
        lit = surfaceVibrancy(lit, p_vibrancy, p_vibrancyDarkness);
        lit += (hashSin(px) - 0.5) * 2.0 * clamp(p_noiseStrength, 0.0, 0.2);

        pane = vec4(lit, pane.a) * mask;
    } else {
        pane = faintTintSlab(tint, tintStrength, mask);
    }

    return slabComposite(slab.window, pane);
}
