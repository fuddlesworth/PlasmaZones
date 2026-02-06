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

    // Base fluid surface: standing waves with staggered phases so there's always motion
    vec2 p = uv * scale;
    float h = 0.0;

    // Primary undulation — large, slow swells (always at least one is active)
    h += sin(p.x * 1.7 + sin(p.y * 0.8) * 0.5) * cos(t * 0.6) * 0.35;
    h += sin(p.y * 2.1 + sin(p.x * 1.1) * 0.4) * cos(t * 0.9 + 2.1) * 0.30;

    // Cross-pattern standing waves at prime-ratio speeds (never flatten together)
    h += sin(p.x * 2.8) * sin(p.y * 2.3) * cos(t * 1.3 + 0.7) * 0.22;
    h += cos(p.x * 1.5 + p.y * 1.9) * sin(t * 0.7 + 3.8) * 0.18;

    // Medium-frequency detail — gives the surface visible texture
    h += sin(p.x * 4.5 + p.y * 1.2) * cos(t * 1.1 + 1.5) * 0.12 * fluidity;
    h += cos(p.x * 1.8 - p.y * 3.7) * sin(t * 1.5 + 0.3) * 0.10 * fluidity;

    // Organic turbulence: FBM that slowly morphs in place
    float turb = fbm(p * 0.8 + vec2(sin(t * 0.15) * 0.4, cos(t * 0.12) * 0.4), 4);
    h += (turb - 0.5) * 0.25 * fluidity;

    // Fine surface tension ripples (dampened by viscosity)
    float fine = sin(p.x * 7.0 + t * 0.3) * sin(p.y * 6.5 - t * 0.2) * cos(t * 2.0);
    h += fine * 0.06 * fluidity;

    // Mouse interaction: radial ripples emanating from cursor
    if (mouseStr > 0.001) {
        vec2 mouseUV = vec2(iMouse.z, 1.0 - iMouse.w); // flip Y to match texture coords
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

    // Approximate surface velocity from time derivatives of the primary waves
    float velocity = 0.0;
    velocity += sin(p.x * 1.7 + sin(p.y * 0.8) * 0.5) * (-0.6 * sin(t * 0.6)) * 0.35;
    velocity += sin(p.y * 2.1 + sin(p.x * 1.1) * 0.4) * (-0.9 * sin(t * 0.9 + 2.1)) * 0.30;
    velocity += sin(p.x * 2.8) * sin(p.y * 2.3) * (-1.3 * sin(t * 1.3 + 0.7)) * 0.22;

    // Caustic intensity: bright spots where waves focus light
    float caustic = abs(velocity) * 2.5 + turb * 0.6;
    caustic = pow(max(caustic, 0.0), 1.3) * 0.5;

    // Pack: R = height (-1..1 range, stored as 0..1), G = velocity, B = caustic
    fragColor = vec4(h * 0.5 + 0.5, velocity * 0.5 + 0.5, caustic, 1.0);
}
