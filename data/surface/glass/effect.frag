// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glass pack, main pass: a refracting pane over the blurred backdrop
// (buffer 1) — a full port of kwin-effects-glass (glass.glsl /
// snells-glass.glsl / oklab.glsl / the noise pass). Two refraction modes,
// switched by p_physicallyBased exactly like the reference:
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
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the refracted backdrop.
// DAEMON FALLBACK: no scene behind daemon surfaces (uHasBackdrop = 0), so
// the pane degrades to a faint tint slab with the same corner rounding.

#version 450
#include <surface_lib.glsl>
#include <surface_noise.glsl>
#include <surface_color.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    FrameSDF fs = frameSdf(px, p_cornerRadius * uSurfaceScale);
    vec2 halfSz = fs.halfSize;
    vec2 pos = px - fs.center; // pane-centered, top-down device px
    float minHalf = min(halfSz.x, halfSz.y);
    float radius = fs.radius;
    float d = fs.d;
    float mask = frameMask(d);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Bevel profile from the VISUAL-radius distance (reference glass()).
        float edgePx = clamp(p_edgeWidth * uSurfaceScale, 0.1, minHalf * 0.9);
        float edgeFactor = 1.0 - clamp(abs(d) / edgePx, 0.0, 1.0);
        float eased = smoothstep(0.0, 1.0, edgeFactor);
        float concave = 1.0 - sqrt(max(1.0 - eased * eased, 0.0));

        float strength = clamp(p_refractionStrength, 0.0, 2.0);
        float fringe = clamp(p_fringing, 0.0, 1.0) * 0.3;
        // Refraction SDF radius: 2x visual, clamped 64..128 logical px —
        // the broadly-curved normal field that wraps the lens around
        // corners (reference glass() line: clamp(cornerRadius * 2, ...)).
        float rr = clamp(radius * 2.0, min(64.0 * uSurfaceScale, minHalf), min(128.0 * uSurfaceScale, minHalf));

        vec3 lit;
        if (p_physicallyBased >= 0.5) {
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
            vec4 g = texture(iChannel1, clamp(vTexCoord + shiftG, 0.0, 1.0));
            lit = g.rgb;
            if (fringe > 0.001) {
                vec2 shiftR = pxToUv(dirPx * (magnitude * (1.0 + fringe))) + lensShift;
                vec2 shiftB = pxToUv(dirPx * (magnitude * (1.0 - fringe))) + lensShift;
                lit.r = texture(iChannel1, clamp(vTexCoord + shiftR, 0.0, 1.0)).r;
                lit.b = texture(iChannel1, clamp(vTexCoord + shiftB, 0.0, 1.0)).b;
            }
            pane.a = g.a;
        } else {
            // ── Cheap mode (reference glassRefraction) ───────────────────
            const float h = 1.0;
            vec2 grad = vec2(sdRoundedBox(pos + vec2(h, 0.0), halfSz, rr) - sdRoundedBox(pos - vec2(h, 0.0), halfSz, rr),
                             sdRoundedBox(pos + vec2(0.0, h), halfSz, rr) - sdRoundedBox(pos - vec2(0.0, h), halfSz, rr));
            vec2 inward = length(grad) > 0.0 ? -normalize(grad) : vec2(0.0, 1.0);
            float strengthUv = min(0.4 * concave * strength, 1.0);
            vec2 dirUv = vec2(inward.x, -inward.y) * strengthUv * (uSurfaceFrameSize / max(uSurfaceSize, vec2(1.0)));
            vec4 g = texture(iChannel1, clamp(vTexCoord + dirUv, 0.0, 1.0));
            lit = vec3(texture(iChannel1, clamp(vTexCoord + dirUv * (1.0 + fringe), 0.0, 1.0)).r, g.g,
                       texture(iChannel1, clamp(vTexCoord + dirUv * (1.0 - fringe), 0.0, 1.0)).b);
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
        const vec3 grayW = vec3(0.299, 0.587, 0.114);
        float tintAdj = tintStrength * clamp(abs(dot(lit, grayW) - dot(tint, grayW)), 0.0, 1.0);
        lit = mix(lit, tint * pane.a, tintAdj);
        lit = oklabSaturate(lit, clamp(p_saturation, 0.0, 2.0));
        lit += (hashSin(px) - 0.5) * 2.0 * clamp(p_noiseStrength, 0.0, 0.2);

        pane = vec4(lit, pane.a) * mask;
    } else {
        pane = vec4(tint, 1.0) * (0.35 * tintStrength) * mask;
    }

    fragColor = slabComposite(window, pane);
}
