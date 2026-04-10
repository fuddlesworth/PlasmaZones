// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Glitch animation — QML wrapper (GLSL 450, UBO, AnimationShaderItem)

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <animation_common.glsl>
#include "glitch.glsl"

void main() {
    float bandCount = customParams[0].x >= 0.0 ? customParams[0].x : 20.0;
    float shiftIntensity = customParams[0].y >= 0.0 ? customParams[0].y : 0.1;
    float rgbSep = customParams[0].z >= 0.0 ? customParams[0].z : 0.02;

    vec2 uv = pzGlitchShift(vTexCoord, pz_progress, bandCount, shiftIntensity);
    float sep = pzGlitchSeparation(pz_progress, rgbSep);
    vec2 uvR = clamp(uv + vec2(sep, 0.0), vec2(0.0), vec2(1.0));
    vec2 uvB = clamp(uv - vec2(sep, 0.0), vec2(0.0), vec2(1.0));

    float r = texture(contentTexture, uvR).r;
    vec4 center = texture(contentTexture, uv);
    float b = texture(contentTexture, uvB).b;

    float fade = pzGlitchFade(pz_progress);
    vec4 color = vec4(r, center.g, b, center.a) * fade;
    fragColor = pzPremultiply(color) * qt_Opacity;
}
