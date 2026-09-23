// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Standard buffer pass: dual Kawase UP level 0 (1/16 res). Reads the deepest
// down level when the radius asks for four levels; otherwise its output is
// unused by the chain and it passes level 2 through. Part of the
// builtin:kawase-* chain (see surface_blur.glsl).

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    int depth = surfaceKawaseDepth();
    float o = surfaceKawaseOffset(depth);
    fragColor = depth >= 4 ? surfaceKawaseUp(iChannel3, vTexCoord, o) : texture(iChannel2, vTexCoord);
}
