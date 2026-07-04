// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glass pack, main pass: a refracting pane over the blurred backdrop
// (buffer 1), a faithful port of kwin-effects-glass' cheap refraction mode
// (glass.glsl). The load-bearing details, matched deliberately:
//
//   - The bevel NORMALS come from a much rounder SDF than the visible
//     corners: the reference inflates the corner radius to 2x, clamped into
//     64..128 px, so the lens bends broadly around corners. Using the
//     visual radius gives axis-aligned normals and a flat, frost-like bend.
//   - The refraction samples INWARD (normal = -normalize(gradient), coord =
//     uv - (-normal * strength)): the rim magnifies interior content like a
//     bulging lens. Sampling outward reads as edge compression instead.
//   - Displacement is a FRACTION OF THE PANE (up to 0.4 x strength at the
//     rim), not a few pixels.
//   - A 2..3 px "thickness glint" band at the rim mixes toward white,
//     weighted by position across the pane (top highlight, bottom shadow),
//     which sells the pane as a physical slab.
//   - Tint strength is luminance-adaptive: it scales with the gray-distance
//     between the refracted backdrop and the tint colour, so the tint never
//     washes out content of similar brightness.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the refracted backdrop.
// DAEMON FALLBACK: no scene behind daemon surfaces (uHasBackdrop = 0), so
// the pane degrades to a faint tint slab with the same corner rounding.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 center = uSurfaceFrameTopLeft + halfSz;
    vec2 pos = px - center; // pane-centered, top-down device px
    float minHalf = min(halfSz.x, halfSz.y);
    float radius = clamp(p_cornerRadius * uSurfaceScale, 0.0, minHalf);
    float d = sdRoundedBox(pos, halfSz, radius);
    float mask = 1.0 - smoothstep(-1.0, 1.0, d);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Bevel profile from the VISUAL-radius distance (reference: glass()
        // computes edgeFactor/concaveFactor from the un-inflated SDF).
        float edgePx = clamp(p_edgeWidth * uSurfaceScale, 0.1, minHalf * 0.9);
        float edgeFactor = 1.0 - clamp(abs(d) / edgePx, 0.0, 1.0);
        float eased = smoothstep(0.0, 1.0, edgeFactor);
        float concave = 1.0 - sqrt(max(1.0 - eased * eased, 0.0));

        // Bevel normal from the INFLATED-radius SDF (2x visual radius,
        // clamped 64..128 logical px, never past the pane's half extent).
        float rr = clamp(radius * 2.0, min(64.0 * uSurfaceScale, minHalf), min(128.0 * uSurfaceScale, minHalf));
        const float h = 1.0;
        vec2 grad = vec2(sdRoundedBox(pos + vec2(h, 0.0), halfSz, rr) - sdRoundedBox(pos - vec2(h, 0.0), halfSz, rr),
                         sdRoundedBox(pos + vec2(0.0, h), halfSz, rr) - sdRoundedBox(pos - vec2(0.0, h), halfSz, rr));
        vec2 inward = length(grad) > 0.0 ? -normalize(grad) : vec2(0.0, 1.0);

        // Reference magnitude and DIRECTION: displace up to 0.4 x strength
        // of the pane extent at the rim, sampling INWARD (rim magnifies).
        // The inward normal is in top-down pixel space; canvas UV is Y-up,
        // so flip its Y, and scale from pane-relative to canvas UV.
        float strengthUv = min(0.4 * concave * clamp(p_refractionStrength, 0.0, 2.0), 1.0);
        vec2 dirUv = vec2(inward.x, -inward.y) * strengthUv * (uSurfaceFrameSize / max(uSurfaceSize, vec2(1.0)));
        float fringe = clamp(p_fringing, 0.0, 1.0) * 0.3;
        float r = texture(iChannel1, clamp(vTexCoord + dirUv * (1.0 + fringe), 0.0, 1.0)).r;
        vec4 g = texture(iChannel1, clamp(vTexCoord + dirUv, 0.0, 1.0));
        float b = texture(iChannel1, clamp(vTexCoord + dirUv * (1.0 - fringe), 0.0, 1.0)).b;
        vec3 lit = vec3(r, g.g, b);

        // Rim glow (reference glassOutline): a soft mix toward the rim
        // colour near the bevel, focus-dimmed like the other packs.
        float focusDim = mix(0.55, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));
        float rimStrength = clamp(p_rimStrength, 0.0, 1.0) * focusDim;
        float rimMask = clamp(0.25 * concave, 0.0, rimStrength);
        lit = mix(lit, p_rimColor.rgb, rimMask);

        // Thickness glint: a 2..3 px band just inside the rim mixed toward
        // white, weighted by position across the pane (top highlight /
        // bottom shadow), selling the pane as a physical slab.
        if (rimStrength > 0.0) {
            float edgeMask = smoothstep(0.0, -2.0, d);
            float borderInner = smoothstep(-1.0, -3.0, d);
            float edgeProfile = pow(max(edgeMask - borderInner, 0.0), 0.9);
            float shadowMask = smoothstep(halfSz.y * 1.4, -halfSz.y * 1.4, pos.y)
                * smoothstep(halfSz.x * 1.4, -halfSz.x * 1.4, pos.x);
            float highlightMask = smoothstep(-halfSz.y * 1.4, halfSz.y * 1.4, pos.y)
                * smoothstep(-halfSz.x * 1.4, halfSz.x * 1.4, pos.x);
            lit = mix(lit, vec3(1.0), edgeProfile * shadowMask);
            lit = mix(lit, vec3(1.0), edgeProfile * highlightMask);
        }

        // Luminance-adaptive tint (reference adjustedTintStrength).
        const vec3 grayW = vec3(0.299, 0.587, 0.114);
        float tintAdj = tintStrength * clamp(abs(dot(lit, grayW) - dot(tint, grayW)), 0.0, 1.0);
        pane = vec4(mix(lit, tint * g.a, tintAdj), g.a) * mask;
    } else {
        pane = vec4(tint, 1.0) * (0.35 * tintStrength) * mask;
    }

    fragColor = window + pane * (1.0 - window.a);
}
