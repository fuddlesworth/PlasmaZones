// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Depth buffer support for PlasmaZones shaders.
// Include to read the depth buffer from a previous pass.
//
// OPT IN with `"depthBuffer": true` in metadata.json. Without it no depth
// attachment is allocated and this sampler reads nothing.
//
// To WRITE depth, declare in your fragment shader:
//   layout(location = 1) out float oDepth;
// and assign: oDepth = yourDepthValue;
//
// Writing is a BUFFER-PASS concern: the main pass has no second colour
// attachment to write to. In a multi-buffer chain every pass shares the ONE
// depth attachment, so the last pass to write wins and that is what the main
// pass reads.
//
// readDepth() takes the same top-down UV every other sampler in this contract
// takes. uDepthBuffer is colour attachment 1 of the same render target the
// channels come from, so it needs the same flip they do: pass it a uv you
// built with channelUv(), not a raw vTexCoord.

#ifndef PLASMAZONES_DEPTH_GLSL
#define PLASMAZONES_DEPTH_GLSL

layout(binding = 16) uniform sampler2D uDepthBuffer;

// Read depth value at UV coordinates
float readDepth(vec2 uv) {
    return texture(uDepthBuffer, uv).r;
}

#endif
