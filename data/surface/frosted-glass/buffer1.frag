// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Buffer pass 1: VERTICAL half of the shared separable Gaussian over buffer 0's
// horizontal result (iChannel0). Kernel in data/surface/shared/surface_blur.glsl;
// the main pass samples this buffer as iChannel1.

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = surfaceGaussianChannelV(vTexCoord);
}
