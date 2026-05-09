// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frosted glass panel shader for PhosphorShell.
//
// Parameters (via customParams):
//   customParams[0].x = tint opacity (0.0 - 1.0, default 0.7)
//   customParams[0].y = noise amount (0.0 - 0.1, default 0.03)
//   customParams[0].z = noise scale (1.0 - 100.0, default 40.0)
//   customParams[0].w = animation speed (0.0 - 2.0, default 0.3)
//
// Colors (via customColors):
//   customColors[0] = tint color (default: dark blue-gray)

#include "common.glsl"

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    float tintOpacity = customParams[0].x > 0.0 ? customParams[0].x : 0.7;
    float noiseAmount = customParams[0].y > 0.0 ? customParams[0].y : 0.03;
    float noiseScale = customParams[0].z > 0.0 ? customParams[0].z : 40.0;
    float animSpeed = customParams[0].w > 0.0 ? customParams[0].w : 0.3;

    vec4 tintColor = customColors[0].a > 0.0 ? customColors[0] : vec4(0.118, 0.118, 0.180, 1.0);

    // Animated noise for frost grain
    vec2 noiseUv = uv * noiseScale + vec2(iTime * animSpeed, iTime * animSpeed * 0.7);
    float grain = fbm(noiseUv);
    float fineGrain = noise(uv * noiseScale * 3.0 + iTime * 0.5);

    // Subtle color variation from noise
    vec3 frostTint = tintColor.rgb + vec3(grain - 0.5, grain - 0.5, fineGrain - 0.5) * noiseAmount;

    // Edge darkening for depth
    float vignette = 1.0 - length((uv - 0.5) * vec2(0.3, 1.0)) * 0.2;

    vec3 color = frostTint * vignette;
    float alpha = tintOpacity;

    fragColor = vec4(color, alpha);
}
