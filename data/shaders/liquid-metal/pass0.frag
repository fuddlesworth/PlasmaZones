// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Liquid Metal — Buffer Pass 0: Height Field Generation
// Generates a fluid-like height map using layered sine ripples with
// mouse-driven disturbance. Output: R = height, G = velocity (for normals),
// B = caustic intensity, A = 1.0.

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <common.glsl>

// Smooth noise for organic ripple shapes
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// FBM with domain rotation between octaves
float fbm(vec2 p, int octaves) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < octaves; i++) {
        val += amp * valueNoise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
        p = rot * p;
    }
    return val;
}

void main() {
    vec2 fragCoord = fragCoordFromTexCoord(vTexCoord);
    vec2 uv = fragCoord / iResolution;

    // Load parameters
    float speed = customParams[0].x;
    float scale = customParams[0].y > 0.5 ? customParams[0].y : 4.0;
    float viscosity = customParams[0].z > 0.05 ? customParams[0].z : 0.6;
    float mouseStr = customParams[2].y;

    float t = iTime * speed;

    // Base fluid surface: standing waves (wobble in place, no net drift)
    vec2 p = uv * scale;
    float h = 0.0;

    // Primary standing waves: spatial pattern modulated by time
    h += sin(p.x * 1.7 + sin(p.y * 0.8) * 0.5) * cos(t * 0.6) * 0.20;
    h += sin(p.y * 2.1 + sin(p.x * 1.1) * 0.4) * cos(t * 0.5 + 1.0) * 0.18;
    h += sin(p.x * 1.3 - p.y * 0.9) * cos(t * 0.35 + 2.5) * 0.15;

    // Secondary standing waves: smaller cross-pattern wobble
    float visc = 1.0 - viscosity; // higher viscosity = less fine detail
    h += sin(p.x * 3.5 + p.y * 2.0) * cos(t * 0.9) * 0.08 * visc;
    h += sin(p.x * 2.0 - p.y * 3.8) * cos(t * 0.7 + 1.5) * 0.06 * visc;

    // Organic turbulence: slowly morphing surface texture (no drift)
    float turb = fbm(p * 0.8 + vec2(sin(t * 0.1) * 0.3, cos(t * 0.08) * 0.3), 4);
    h += (turb - 0.5) * 0.15 * visc;

    // Fine surface tension dimples
    float fine = sin(p.x * 8.0) * sin(p.y * 7.0) * cos(t * 1.2);
    h += fine * 0.02 * visc;

    // Mouse interaction: radial ripples emanating from cursor
    if (mouseStr > 0.001) {
        vec2 mouseUV = vec2(iMouse.z, 1.0 - iMouse.w); // flip Y to match screen-space UV
        if (mouseUV.x > 0.0 || mouseUV.y > 0.0) {
            float dist = length(uv - mouseUV);
            // Expanding ring ripple
            float ring = sin(dist * 30.0 - t * 4.0) * exp(-dist * 5.0);
            h += ring * mouseStr * 0.5;
            // Displacement push
            float push = exp(-dist * 8.0);
            h += push * mouseStr * 0.3;
        }
    }

    // Compute approximate surface velocity (time derivative of primary waves)
    float velocity = 0.0;
    velocity += sin(p.x * 1.7 + sin(p.y * 0.8) * 0.5) * (-sin(t * 0.6)) * 0.6 * 0.20;
    velocity += sin(p.y * 2.1 + sin(p.x * 1.1) * 0.4) * (-sin(t * 0.5 + 1.0)) * 0.5 * 0.18;

    // Caustic intensity: bright spots where waves focus light
    // Approximate as areas of high curvature convergence
    float caustic = abs(velocity) * 2.0 + turb * 0.5;
    caustic = pow(max(caustic, 0.0), 1.5) * 0.3;

    // Pack: R = height (-1..1 range, stored as 0..1), G = velocity, B = caustic
    fragColor = vec4(h * 0.5 + 0.5, velocity * 0.5 + 0.5, caustic, 1.0);
}
