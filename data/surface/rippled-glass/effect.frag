// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rippled-glass pack, main pass: an INTERIOR-refracting pane over the blurred
// backdrop (buffer 1). Where the Glass pack bends the backdrop only along an
// edge bevel, this pack warps it across the WHOLE pane: a two-octave value-
// noise height field models the uneven glass surface, its gradient displaces
// the backdrop sample (gradient refraction — light bends toward the slope),
// and the same gradient feeds a soft directional highlight along the ripple
// crests. Chromatic fringing splits the red/blue samples along the
// displacement, matching the Glass pack's convention (p_fringing scaled by
// 0.3). The field drifts on iTime at p_rippleSpeed; 0 freezes it, giving the
// static bathroom-window look.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the rippled backdrop.
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

// px-space (top-down) vector -> canvas UV offset (vTexCoord is Y-up).
vec2 pxToUv(vec2 v) {
    return vec2(v.x, -v.y) / max(uSurfaceSize, vec2(1.0));
}

// High-quality hash, avoids directional artifacts (same as frosted-glass).
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Smooth value noise with quintic interpolation.
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// The glass surface's height at ripple-space q: a dominant swell plus a
// finer counter-drifting octave, so the warp reads as organic ripples
// rather than a repeating wobble.
float rippleHeight(vec2 q, float t) {
    float swell = vnoise(q + vec2(t * 0.31, t * 0.23));
    float detail = vnoise(q * 2.7 + vec2(11.3, 7.1) - vec2(t * 0.17, t * 0.29));
    return swell * 0.65 + detail * 0.35;
}

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 center = uSurfaceFrameTopLeft + halfSz;
    float minHalf = min(halfSz.x, halfSz.y);
    float radius = clamp(p_cornerRadius * uSurfaceScale, 0.0, minHalf);
    float d = sdRoundedBox(px - center, halfSz, radius);
    float mask = 1.0 - smoothstep(-1.0, 1.0, d);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Ripple-space coordinate: p_rippleSize is the ripple's logical-px
        // size, so the pattern is DPI-stable and does not stretch with the
        // pane (unlike a frame-normalized uv).
        float sizePx = max(p_rippleSize, 1.0) * uSurfaceScale;
        vec2 q = px / sizePx;
        float t = iTime * max(p_rippleSpeed, 0.0);

        // Central-difference gradient of the height field. The epsilon is a
        // fixed fraction of a ripple so the slope estimate stays smooth at
        // any ripple size.
        const float e = 0.35;
        vec2 grad = vec2(rippleHeight(q + vec2(e, 0.0), t) - rippleHeight(q - vec2(e, 0.0), t),
                         rippleHeight(q + vec2(0.0, e), t) - rippleHeight(q - vec2(0.0, e), t))
            / (2.0 * e);

        // Gradient refraction: displace the backdrop sample down-slope by up
        // to p_refractionStrength logical px, split R/B for the fringing.
        vec2 dispPx = grad * clamp(p_refractionStrength, 0.0, 40.0) * uSurfaceScale;
        vec2 shift = pxToUv(dispPx);
        float fringe = clamp(p_fringing, 0.0, 1.0) * 0.3;
        vec4 g = texture(iChannel1, clamp(vTexCoord + shift, 0.0, 1.0));
        vec3 lit = g.rgb;
        if (fringe > 0.001) {
            lit.r = texture(iChannel1, clamp(vTexCoord + shift * (1.0 + fringe), 0.0, 1.0)).r;
            lit.b = texture(iChannel1, clamp(vTexCoord + shift * (1.0 - fringe), 0.0, 1.0)).b;
        }

        // Soft directional highlight: slopes facing the up-left "light"
        // catch a dim glint, scaled by how steep the ripple is — flat glass
        // stays clean. Premultiplied add, weighted by the backdrop alpha so
        // the glint never brightens the cleared off-capture margin.
        float slope = length(grad);
        if (slope > 0.0001) {
            float facing = clamp(dot(grad / slope, vec2(-0.6, 0.8)), 0.0, 1.0);
            float glint = pow(facing * min(slope, 1.0), 2.0) * clamp(p_highlightStrength, 0.0, 1.0);
            lit += glint * g.a;
        }

        lit = mix(lit, tint * g.a, tintStrength);
        pane = vec4(lit, g.a) * mask;
    } else {
        pane = vec4(tint, 1.0) * (0.35 * tintStrength) * mask;
    }

    fragColor = window + pane * (1.0 - window.a);
}
