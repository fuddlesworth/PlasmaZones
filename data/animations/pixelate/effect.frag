// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pixelate animation — QML wrapper (GLSL 450, UBO, AnimationShaderItem)

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <animation_common.glsl>
#include "pixelate.glsl"

void main() {
    float maxPixelSize = customParams[0].x >= 0.0 ? customParams[0].x : 40.0;
    float fadeStart = customParams[0].y >= 0.0 ? customParams[0].y : 0.7;

    vec2 coord = pzPixelateCoord(vTexCoord, iResolution, pz_progress, maxPixelSize);
    vec4 tex = texture(contentTexture, coord);
    float fade = pzPixelateFade(pz_progress, fadeStart);
    fragColor = pzPremultiply(vec4(tex.rgb, tex.a * fade)) * qt_Opacity;
}
