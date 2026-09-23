// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Standard buffer pass: dual Kawase DOWN level 1, halving iChannel0. Part of
// the builtin:kawase-* chain (see surface_blur.glsl).

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = surfaceKawaseDown(iChannel0, vTexCoord, surfaceKawaseOffset(surfaceKawaseDepth()));
}
