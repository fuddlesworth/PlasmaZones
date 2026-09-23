// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Standard buffer pass: dual Kawase UP level 1 (1/8 res). Reads the up pass
// below it at depth 4, the matching down level at depth 3, and passes level 1
// through when the pyramid is shallower. Part of the builtin:kawase-* chain
// (see surface_blur.glsl).

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    int depth = surfaceKawaseDepth();
    float o = surfaceKawaseOffset(depth);
    if (depth >= 4) {
        fragColor = surfaceKawaseUp(iChannel4, vTexCoord, o);
    } else if (depth == 3) {
        fragColor = surfaceKawaseUp(iChannel2, vTexCoord, o);
    } else {
        fragColor = texture(iChannel1, vTexCoord);
    }
}
