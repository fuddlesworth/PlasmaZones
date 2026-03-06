// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Multipass bindings. Include in effect.frag when using buffer channels (metadata multipass: true).
// Declares iChannel0-3 and channelUv. Include after ZoneUniforms (needs iChannelResolution[4]).
//
//   #include <multipass.glsl>

#ifndef PLASMAZONES_MULTIPASS_GLSL
#define PLASMAZONES_MULTIPASS_GLSL

layout(binding = 2) uniform sampler2D iChannel0;
layout(binding = 3) uniform sampler2D iChannel1;
layout(binding = 4) uniform sampler2D iChannel2;
layout(binding = 5) uniform sampler2D iChannel3;

// Returns UV for sampling iChannel[channelIndex] at the given fragCoord.
// fragCoord uses Y=0-at-top convention (matching iResolution / Shadertoy style).
//
// On OpenGL (iFlipBufferY=1), FBO textures store row 0 at the physical bottom
// of GPU memory, so standard UV sampling would invert the result. On Vulkan/Metal
// (iFlipBufferY=0), FBOs store row 0 at the top, matching UV convention directly.
//
// IMPORTANT: Always use channelUv() for ALL iChannel sampling — never sample
// iChannel textures with raw vTexCoord or manual UV, as this bypasses the
// backend-specific Y correction.
vec2 channelUv(int channelIndex, vec2 fragCoord) {
    vec2 r = max(iChannelResolution[channelIndex], vec2(1.0));
    vec2 uv = fragCoord / r;
    if (iFlipBufferY != 0) {
        uv.y = 1.0 - uv.y;
    }
    return uv;
}

#endif
