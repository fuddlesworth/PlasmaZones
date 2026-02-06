// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Voronoi Stained Glass — Buffer Pass 1: Horizontal Bloom Blur
// Extracts bright areas from the 3D scene (iChannel0) and applies
// a wide horizontal Gaussian blur for cathedral glow.
// Output to iChannel1: RGB = h-blurred bloom, A = 1.0.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>

// Sample with soft brightness extraction — glass glows, lead stays dark.
vec3 sampleBright(vec2 uv) {
    vec3 c = texture(iChannel0, uv).rgb;
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    return c * smoothstep(0.12, 0.45, lum);
}

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 uv = channelUv(0, fragCoord);
    float px = 1.0 / max(iChannelResolution[0].x, 1.0);

    // 11-tap Gaussian (sigma ~3), doubled spacing for wider bloom
    const int TAPS = 6;
    float w[TAPS] = float[](0.1974, 0.1747, 0.1210, 0.0656, 0.0278, 0.0092);

    vec3 sum = sampleBright(uv) * w[0];
    for (int i = 1; i < TAPS; i++) {
        float off = float(i) * px * 2.0;
        sum += sampleBright(uv + vec2(off, 0.0)) * w[i];
        sum += sampleBright(uv - vec2(off, 0.0)) * w[i];
    }

    fragColor = vec4(sum, 1.0);
}
