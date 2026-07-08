// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Buffer pass 0: HORIZONTAL half of the shared separable Gaussian over the
// backdrop capture. Kernel + the buffer-pass parameter convention live in
// data/surface/shared/surface_blur.glsl; this pass is a thin caller so the
// weights are defined once for the whole blur family.

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = surfaceGaussianBackdropH(vTexCoord);
}
