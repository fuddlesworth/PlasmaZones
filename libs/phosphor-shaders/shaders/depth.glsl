// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Depth buffer support for PhosphorShaders shaders.
// Include to read the depth buffer from a previous pass.
// To write depth, declare in your fragment shader:
//   layout(location = 1) out float oDepth;
// and assign: oDepth = yourDepthValue;

#ifndef PHOSPHORSHADERS_DEPTH_GLSL
#define PHOSPHORSHADERS_DEPTH_GLSL

layout(binding = 12) uniform sampler2D uDepthBuffer;

// Read depth value at UV coordinates
float readDepth(vec2 uv) {
    return texture(uDepthBuffer, uv).r;
}

#endif
