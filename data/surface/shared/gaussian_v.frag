// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Standard buffer pass: VERTICAL half of the shared separable Gaussian over
// the horizontal pass's result (iChannel0). Packs opt in via
// `"bufferShaders": ["builtin:gaussian-h", "builtin:gaussian-v"]` in
// metadata.json; the main pass samples this buffer as iChannel1. Kernel in
// surface_blur.glsl.

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = surfaceGaussianChannelV(vTexCoord);
}
