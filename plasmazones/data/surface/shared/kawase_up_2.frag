// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Standard buffer pass: dual Kawase UP level 2, the final pass back at the
// quarter-res base, which the main pass reads as iChannel6 (surfaceBlurTexel).
// Reads the up pass below it at depth 3 or more, the matching down level at
// depth 2, and the base level itself at depth 1. Part of the builtin:kawase-*
// chain (see surface_blur.glsl).

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    int depth = surfaceKawaseDepth();
    float o = surfaceKawaseOffset(depth);
    if (depth >= 3) {
        fragColor = surfaceKawaseUp(iChannel5, vTexCoord, o);
    } else if (depth == 2) {
        fragColor = surfaceKawaseUp(iChannel1, vTexCoord, o);
    } else {
        fragColor = texture(iChannel0, vTexCoord);
    }
}
