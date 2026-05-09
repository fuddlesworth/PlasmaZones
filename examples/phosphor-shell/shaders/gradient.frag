// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animated gradient panel shader for PhosphorShell (self-contained).
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

float roundedBoxSDF(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + r;
    return length(max(d, 0.0)) - r;
}

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / iResolution.xy;

    float speed = customParams[0].x > 0.0 ? customParams[0].x : 0.5;
    float angle = customParams[0].y;
    float radius = customParams[1].x > 0.0 ? customParams[1].x : 12.0;

    // SDF rounded rectangle mask
    vec2 center = iResolution.xy * 0.5;
    vec2 halfSize = iResolution.xy * 0.5;
    float dist = roundedBoxSDF(fragCoord - center, halfSize, radius);
    float mask = 1.0 - smoothstep(-1.0, 1.0, dist);

    if (mask <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    vec4 colorA = customColors[0].a > 0.0 ? customColors[0] : vec4(0.118, 0.118, 0.180, 0.9);
    vec4 colorB = customColors[1].a > 0.0 ? customColors[1] : vec4(0.180, 0.118, 0.235, 0.9);

    vec2 dir = vec2(cos(angle), sin(angle));
    float t = dot(uv, dir);
    t = t + sin(iTime * speed) * 0.1;
    t = clamp(t, 0.0, 1.0);

    vec4 color = mix(colorA, colorB, t);
    color.a *= mask;
    fragColor = color * qt_Opacity;
}
