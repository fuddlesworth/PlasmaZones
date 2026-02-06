// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Liquid Metal — Buffer Pass 2: Bloom + Tone Mapping
// Applies a separable bloom to the lit surface from pass1 (iChannel1),
// then tone-maps for HDR specular highlights.
// Output: RGB = final tonemapped image, A = 1.0.

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>

// Extract bright pixels for bloom
vec3 sampleBright(vec2 uv) {
    vec3 c = texture(iChannel1, uv).rgb;
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    return c * smoothstep(0.7, 1.2, lum);
}

// ACES filmic tone mapping
vec3 aces(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 fragCoord = fragCoordFromTexCoord(vTexCoord);
    vec2 uv = fragCoord / iResolution;
    vec2 texel = 1.0 / max(iResolution, vec2(1.0));

    // Base color from pass1
    vec3 base = texture(iChannel1, uv).rgb;

    // 11-tap separable bloom (combined H+V in single pass for efficiency)
    vec3 bloom = vec3(0.0);
    float weights[6] = float[](0.227, 0.194, 0.122, 0.054, 0.016, 0.003);

    // Horizontal taps
    bloom += sampleBright(uv) * weights[0];
    for (int i = 1; i < 6; i++) {
        vec2 off = vec2(float(i) * 2.0 * texel.x, 0.0);
        bloom += sampleBright(uv + off) * weights[i];
        bloom += sampleBright(uv - off) * weights[i];
    }

    // Vertical taps
    for (int i = 1; i < 6; i++) {
        vec2 off = vec2(0.0, float(i) * 2.0 * texel.y);
        bloom += sampleBright(uv + off) * weights[i];
        bloom += sampleBright(uv - off) * weights[i];
    }

    // Combine: base + bloom
    vec3 col = base + bloom * 0.4;

    // ACES tone mapping for HDR highlights
    col = aces(col);

    // Slight vignette for depth
    float vig = 1.0 - length(uv - 0.5) * 0.3;
    col *= vig;

    fragColor = vec4(col, 1.0);
}
