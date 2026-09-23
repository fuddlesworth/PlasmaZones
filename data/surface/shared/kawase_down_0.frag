// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Standard buffer pass: dual Kawase DOWN level 0, the backdrop capture into
// the quarter-res base. Packs opt in with the seven builtin:kawase-* tokens
// (see surface_blur.glsl for the chain and its bufferScales).

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = surfaceKawaseDownBackdrop(vTexCoord, surfaceKawaseOffset(surfaceKawaseDepth()));
}
