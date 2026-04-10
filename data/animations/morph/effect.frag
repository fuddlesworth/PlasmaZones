// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Morph animation shader — passthrough fragment shader.
// Geometry interpolation is handled by the host (C++ translate + scale).

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <animation_common.glsl>

void main() {
    vec4 tex = texture(contentTexture, vTexCoord);
    fragColor = pzPremultiply(tex) * qt_Opacity;
}
