// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animated gradient panel shader for PhosphorShell.
//
// Parameters (via customParams):
//   customParams[0].x = animation speed (default 0.5)
//   customParams[0].y = gradient angle in radians (default 0.0 = horizontal)
//
// Colors (via customColors):
//   customColors[0] = start color
//   customColors[1] = end color

#include "common.glsl"

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    float speed = customParams[0].x > 0.0 ? customParams[0].x : 0.5;
    float angle = customParams[0].y;

    vec4 colorA = customColors[0].a > 0.0 ? customColors[0] : vec4(0.118, 0.118, 0.180, 0.9);
    vec4 colorB = customColors[1].a > 0.0 ? customColors[1] : vec4(0.180, 0.118, 0.235, 0.9);

    // Animated gradient position
    vec2 dir = vec2(cos(angle), sin(angle));
    float t = dot(uv, dir);
    t = t + sin(iTime * speed) * 0.1;
    t = clamp(t, 0.0, 1.0);

    fragColor = mix(colorA, colorB, t);
}
