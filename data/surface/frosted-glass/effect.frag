// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frosted-glass pack, main pass: the phosphor-shell frosted panel shader
// (examples/phosphor-shell/shaders/frosted_glass.frag) ported onto a REAL
// blurred backdrop. The original faked frosting with a translucent tint
// slab; here the slab is the Gaussian-blurred scene behind the surface
// (buffer 1), and the original's layers ride on top unchanged: the
// multi-octave crystalline Voronoi grain (slow-drifting on iTime), the
// tint, the multiplicative vignette, and the rounded-corner SDF clip.
//
// DAEMON FALLBACK: daemon-hosted surfaces (OSDs / popups) have no scene
// behind them (uHasBackdrop = 0), so the pack renders the ORIGINAL pseudo
// look there — the translucent tint slab with the same grain and vignette.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the frosted backdrop.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// ── Grain stack, ported verbatim from the shell shader ──────────────────────

// High-quality hash, avoids directional artifacts.
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

// Crystalline Voronoi noise: edge distance between the two nearest cell
// points gives frost-crystal boundaries.
float voronoi(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 1.0;
    float secondMin = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 cellId = i + neighbor;
            float h1 = hash(cellId);
            float h2 = hash(cellId + vec2(127.1, 311.7));
            vec2 diff = neighbor + vec2(h1, h2) - f;
            float dist = dot(diff, diff);
            if (dist < minDist) {
                secondMin = minDist;
                minDist = dist;
            } else if (dist < secondMin) {
                secondMin = dist;
            }
        }
    }
    return sqrt(secondMin) - sqrt(minDist);
}

// Multi-octave crystalline texture: dominant crystal structure + drifting
// fine detail + micro noise.
float frostedTexture(vec2 p, float time) {
    float crystal = voronoi(p);
    float fine = voronoi(p * 2.3 + vec2(time * 0.1, time * 0.07));
    float micro = vnoise(p * 8.0 + time * 0.2);
    return crystal * 0.6 + fine * 0.3 + micro * 0.1;
}

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 center = uSurfaceFrameTopLeft + halfSz;
    float radius = clamp(p_cornerRadius * uSurfaceScale, 0.0, min(halfSz.x, halfSz.y));
    float d = sdRoundedBox(px - center, halfSz, radius);
    float mask = 1.0 - smoothstep(-1.0, 1.0, d);

    // Frame-normalized coordinate for the grain and vignette, so the look
    // scales with the pane like the original's uv did with the panel.
    vec2 fuv = (px - uSurfaceFrameTopLeft) / max(uSurfaceFrameSize, vec2(1.0));

    // Crystalline frost variation, centered around zero (both directions —
    // one-sided clamping made the grain read as dark speckles only).
    float frost = frostedTexture(fuv * p_grainScale, iTime * p_grainSpeed);
    float variation = (frost - 0.5) * p_grainAmount;

    // Vignette darkens edges, multiplicative so it never brightens.
    float vignette = clamp(1.0 - length((fuv - 0.5) * vec2(0.3, 1.0)) * p_vignetteStrength, 0.0, 1.0);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Real frosting: blurred backdrop, tinted, grained, vignetted.
        vec4 blurred = texture(iChannel1, vTexCoord);
        vec3 color = mix(blurred.rgb, tint * blurred.a, tintStrength);
        color = clamp(color + vec3(variation) * blurred.a, 0.0, 1.0 * max(blurred.a, 0.0001));
        color *= vignette;
        pane = vec4(color, blurred.a) * mask;
    } else {
        // Original pseudo look: translucent tint slab + grain + vignette
        // (the shell shader's tintOpacity slab, at the tint strength).
        float slabAlpha = clamp(0.4 + 0.6 * tintStrength, 0.0, 1.0);
        vec3 color = clamp(tint + vec3(variation), 0.0, 1.0) * vignette;
        pane = vec4(color, 1.0) * slabAlpha * mask;
    }

    fragColor = window + pane * (1.0 - window.a);
}
