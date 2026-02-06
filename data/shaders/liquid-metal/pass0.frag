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

    // Fluidity factor: higher viscosity = slower, thicker, less fine detail
    float fluidity = 1.0 - viscosity * 0.7;

    // Base fluid surface: standing waves that breathe in place
    vec2 p = uv * scale;
    float h = 0.0;

    // Primary undulation: spatial pattern modulated by time (no directional drift)
    h += sin(p.x * 1.7 + sin(p.y * 0.8) * 0.5) * cos(t * 0.6) * 0.30;
    h += sin(p.y * 2.1 + sin(p.x * 1.1) * 0.4) * cos(t * 0.5 + 1.0) * 0.25;

    // Cross-pattern standing waves at different phases
    h += sin(p.x * 2.8) * sin(p.y * 2.3) * cos(t * 0.8 + 0.7) * 0.18;
    h += cos(p.x * 1.5 + p.y * 1.9) * sin(t * 0.4 + 2.0) * 0.15;

    // Organic turbulence: FBM that slowly morphs in place
    float turb = fbm(p * 0.8 + vec2(sin(t * 0.1) * 0.3, cos(t * 0.08) * 0.3), 4);
    h += (turb - 0.5) * 0.20 * fluidity;

    // Fine surface tension ripples (standing, dampened by viscosity)
    float fine = sin(p.x * 6.0) * sin(p.y * 5.5) * cos(t * 1.5);
    h += fine * 0.03 * fluidity;

    // Mouse interaction: radial ripples emanating from cursor
    if (mouseStr > 0.001) {
        vec2 mouseUV = iMouse.zw; // normalized 0-1
        if (mouseUV.x > 0.0 || mouseUV.y > 0.0) {
            float dist = length(uv - mouseUV);
            // Standing ripple centered on cursor (concentric rings that pulse)
            float ring = sin(dist * 25.0) * cos(t * 3.0) * exp(-dist * 6.0);
            h += ring * mouseStr * 0.5;
            // Displacement dome under cursor
            float push = exp(-dist * 10.0);
            h += push * mouseStr * 0.25;
        }
    }

    // Compute approximate surface velocity via analytical time derivative
    // of the primary standing waves (d/dt of cos(wt) = -w*sin(wt))
    float velocity = 0.0;
    velocity += sin(p.x * 1.7 + sin(p.y * 0.8) * 0.5) * (-0.6 * sin(t * 0.6)) * 0.30;
    velocity += sin(p.y * 2.1 + sin(p.x * 1.1) * 0.4) * (-0.5 * sin(t * 0.5 + 1.0)) * 0.25;

    // Caustic intensity: bright spots where waves focus light
    // Approximate as areas of high curvature convergence
    float caustic = abs(velocity) * 2.0 + turb * 0.5;
    caustic = pow(max(caustic, 0.0), 1.5) * 0.3;

    // Pack: R = height (-1..1 range, stored as 0..1), G = velocity, B = caustic
    fragColor = vec4(h * 0.5 + 0.5, velocity * 0.5 + 0.5, caustic, 1.0);
}
