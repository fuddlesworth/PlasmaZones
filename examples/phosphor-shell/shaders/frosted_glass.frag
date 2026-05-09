// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frosted glass panel shader for PhosphorShell (self-contained).
//
// customParams[0]: x=tintOpacity y=noiseAmount z=noiseScale w=animSpeed
// customColors[0]: tint color (customColor1 in QML)

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

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;

    float tintOpacity = customParams[0].x > 0.0 ? customParams[0].x : 0.7;
    float noiseAmount = customParams[0].y > 0.0 ? customParams[0].y : 0.03;
    float noiseScale = customParams[0].z > 0.0 ? customParams[0].z : 40.0;
    float animSpeed = customParams[0].w > 0.0 ? customParams[0].w : 0.3;

    vec4 tintColor = customColors[0].a > 0.0 ? customColors[0] : vec4(0.118, 0.118, 0.180, 1.0);

    vec2 noiseUv = uv * noiseScale + vec2(iTime * animSpeed, iTime * animSpeed * 0.7);
    float grain = fbm(noiseUv);
    float fineGrain = noise(uv * noiseScale * 3.0 + iTime * 0.5);

    vec3 frostTint = tintColor.rgb + vec3(grain - 0.5, grain - 0.5, fineGrain - 0.5) * noiseAmount;
    float vignette = 1.0 - length((uv - 0.5) * vec2(0.3, 1.0)) * 0.2;

    vec3 color = frostTint * vignette;
    fragColor = vec4(color, tintOpacity) * qt_Opacity;
}
