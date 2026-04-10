// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pop-in animation shader — opacity fade accompanying center-scale geometry.

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <animation_common.glsl>

void main() {
    vec4 tex = texture(contentTexture, vTexCoord);
    float opacity = clamp(pz_progress, 0.0, 1.0);
    fragColor = pzPremultiply(vec4(tex.rgb, tex.a * opacity)) * qt_Opacity;
}
