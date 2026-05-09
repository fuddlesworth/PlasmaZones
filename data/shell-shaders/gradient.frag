// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animated gradient panel shader for PhosphorShell (self-contained).
// Output is pre-multiplied alpha. SDF mask uses hard cutoff to avoid fringe.
//
// customParams[0]: x=speed y=angle
// customParams[1]: x=cornerRadius (pixels, default 12.0)
// customColors[0]: start color (customColor1 in QML)
// customColors[1]: end color (customColor2 in QML)

#version 450

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int appField0;
    int appField1;
    vec4 iMouse;
    vec4 iDate;
    vec4 customParams[8];
    vec4 customColors[16];
};

layout(location = 0) out vec4 fragColor;

// SDF for a rounded rectangle centered at origin
float roundedBoxSDF(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + r;
    return length(max(d, 0.0)) - r;
}

// Attempt a smooth easing for animation (sine-based, continuous)
float easeInOut(float t) {
    return 0.5 - 0.5 * cos(t * 3.14159265);
}

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / iResolution.xy;

    float speed = customParams[0].x > 0.0 ? customParams[0].x : 0.5;
    float angle = customParams[0].y;
    float radius = customParams[1].x > 0.0 ? customParams[1].x : 12.0;

    // SDF rounded rectangle mask - hard cutoff eliminates fringe
    vec2 center = iResolution.xy * 0.5;
    vec2 halfSize = iResolution.xy * 0.5;
    float dist = roundedBoxSDF(fragCoord - center, halfSize, radius);

    // Smooth AA edge - premultiplied alpha prevents fringe
    float mask = 1.0 - smoothstep(-1.0, 0.0, dist);

    if (mask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    vec4 colorA = customColors[0].a > 0.0 ? customColors[0] : vec4(0.118, 0.118, 0.180, 0.9);
    vec4 colorB = customColors[1].a > 0.0 ? customColors[1] : vec4(0.180, 0.118, 0.235, 0.9);

    // Gradient direction from angle
    vec2 dir = vec2(cos(angle), sin(angle));
    float t = dot(uv - 0.5, dir) + 0.5;

    // Smooth animation offset using sine easing - no abrupt jumps
    float animPhase = iTime * speed;
    float offset = sin(animPhase) * 0.1;
    t = t + offset;

    // Smooth hermite clamp for gradient edges
    t = smoothstep(0.0, 1.0, t);

    // Interpolate colors in linear space
    vec4 color = mix(colorA, colorB, t);

    // Apply mask to alpha
    float alpha = color.a * mask;

    // Pre-multiplied alpha output: RGB * alpha before output
    fragColor = vec4(color.rgb * alpha, alpha) * qt_Opacity;
}
