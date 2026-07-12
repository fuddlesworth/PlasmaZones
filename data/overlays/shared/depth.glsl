// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Depth buffer support for PlasmaZones shaders.
// Include to read the depth buffer from a previous pass.
// To write depth, declare in your fragment shader:
//   layout(location = 1) out float oDepth;
// and assign: oDepth = yourDepthValue;

#ifndef PLASMAZONES_DEPTH_GLSL
#define PLASMAZONES_DEPTH_GLSL

layout(binding = 12) uniform sampler2D uDepthBuffer;

// Read depth value at UV coordinates
float readDepth(vec2 uv) {
    return texture(uDepthBuffer, uv).r;
}

#endif
