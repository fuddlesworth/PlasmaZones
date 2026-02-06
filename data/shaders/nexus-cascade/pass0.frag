// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Nexus Cascade — Buffer Pass 0: Animated plasma / flow base
// Fullscreen output to iChannel0. No channel inputs.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>

// Rotate uv around center by angle (radians)
vec2 rotate2d(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

float plasma(vec2 uv, float t) {
    float v = 0.0;
    float px = uv.x + sin(t * 0.7) * 0.3;
    float py = uv.y + cos(t * 0.5) * 0.2;
    v += sin((px + t * 0.8) * 8.0);
    v += sin((py - t * 0.6) * 7.0);
    v += sin((uv.x + uv.y + t * 1.2 + uv.x * 2.0) * 12.0);
    v += sin(length(uv - 0.5) * 15.0 - t * 2.0);
    v += sin(atan(uv.y - 0.5, uv.x - 0.5) * 4.0 + t * 1.5) * 0.4;
    return v * 0.25 + 0.5;
}

float flowNoise(vec2 p, float t) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    float n = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
    float angle = atan(p.y, p.x);
    float r = length(p);
    float swirl = sin(angle * 3.0 + t * 2.0) * 0.5 + sin(r * 2.0 - t * 1.2) * 0.5;
    return n + 0.15 * swirl;
}

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 uv = fragCoord / max(iResolution.xy, vec2(1.0));
    vec2 centered = uv - 0.5;

    float speed = customParams[0].x > 0.001 ? customParams[0].x : 0.4;
    float scale = customParams[0].y > 0.1 ? customParams[0].y : 4.0;
    float t = iTime * speed;

    float swirlA = t * 0.4;
    float swirlB = t * -0.25;
    vec2 uvPlasma = rotate2d(centered, swirlA) + 0.5;
    vec2 uvFlow = rotate2d(centered, swirlB) + 0.5;

    float p = plasma(uvPlasma * scale, t);
    float f = flowNoise(uvFlow * scale * 2.0, t);

    vec3 c1 = colorWithFallback(customColors[0].rgb, vec3(0.35, 0.55, 0.95));
    vec3 c2 = colorWithFallback(customColors[1].rgb, vec3(0.9, 0.4, 0.85));
    vec3 c3 = colorWithFallback(customColors[2].rgb, vec3(0.2, 0.8, 0.75));

    vec3 col = mix(c1, c2, p);
    col = mix(col, c3, f * 0.5);
    float mod = 0.65 + 0.35 * (p * f + sin(t + uvPlasma.x * TAU + uvFlow.y * 4.0) * 0.15);
    col *= mod;

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
