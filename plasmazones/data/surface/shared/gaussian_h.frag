// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Standard buffer pass: HORIZONTAL half of the shared separable Gaussian over
// the backdrop capture. Packs opt in via `"bufferShaders": ["builtin:gaussian-h",
// "builtin:gaussian-v"]` in metadata.json; the registry resolves the builtin
// token to this file so the blur family shares one pass source instead of a
// per-pack wrapper. Kernel + the buffer-pass parameter convention live in
// surface_blur.glsl.

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = surfaceGaussianBackdropH(vTexCoord);
}
