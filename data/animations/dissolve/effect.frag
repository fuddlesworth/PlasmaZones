// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Dissolve animation — QML wrapper (GLSL 450, UBO, AnimationShaderItem)

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <animation_common.glsl>
#include "dissolve.glsl"

void main() {
    float noiseScale = customParams[0].x >= 0.0 ? customParams[0].x : 200.0;
    float edgeSoftness = customParams[0].y >= 0.0 ? customParams[0].y : 0.05;

    vec4 tex = texture(contentTexture, vTexCoord);
    float alpha = pzDissolveAlpha(vTexCoord, pz_progress, noiseScale, edgeSoftness);
    fragColor = pzPremultiply(vec4(tex.rgb, tex.a * alpha)) * qt_Opacity;
}
