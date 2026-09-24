// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Multipass bindings. Include in effect.frag when using buffer channels (metadata multipass: true).
// Declares iChannel0-7 and channelUv. Include after ZoneUniforms (needs iChannelResolution[4]).
//
//   #include <multipass.glsl>
//
// Bindings 2..9 are the channel block of the shared binding table every RHI
// family declares (PhosphorShaders/ShaderBindings.h). The UBO's
// iChannelResolution[4] covers the first four channels only; channelUv sizes
// the later ones with textureSize().

#ifndef PLASMAZONES_MULTIPASS_GLSL
#define PLASMAZONES_MULTIPASS_GLSL

layout(binding = 2) uniform sampler2D iChannel0;
layout(binding = 3) uniform sampler2D iChannel1;
layout(binding = 4) uniform sampler2D iChannel2;
layout(binding = 5) uniform sampler2D iChannel3;
layout(binding = 6) uniform sampler2D iChannel4;
layout(binding = 7) uniform sampler2D iChannel5;
layout(binding = 8) uniform sampler2D iChannel6;
layout(binding = 9) uniform sampler2D iChannel7;

// Pixel size of iChannel[channelIndex]. The first four come from the UBO
// (iChannelResolution), the rest from the sampler itself: the UBO keeps its
// 672-byte base layout, so it never grew past four slots.
vec2 channelSize(int channelIndex) {
    switch (channelIndex) {
    case 0: case 1: case 2: case 3:
        return iChannelResolution[channelIndex];
    case 4: return vec2(textureSize(iChannel4, 0));
    case 5: return vec2(textureSize(iChannel5, 0));
    case 6: return vec2(textureSize(iChannel6, 0));
    case 7: return vec2(textureSize(iChannel7, 0));
    default: return vec2(1.0);
    }
}

// Returns UV for sampling iChannel[channelIndex] at the given fragCoord.
// fragCoord is Y=0-AT-TOP here, which is NOT Shadertoy's convention (that one
// is bottom-origin); it matches the rest of this contract, where every pixel
// space is top-down.
//
// Y is always flipped below: both OpenGL (Y-up FBO) and Vulkan (negative-height
// viewport) store buffer data that needs a flip when sampled. The flip is
// HARDCODED here rather than read from iFlipBufferY. That uniform is always 1
// and no GLSL in the tree reads it, so it is not the authority for this line.
//
// IMPORTANT: Always use channelUv() for ALL iChannel sampling — never sample
// iChannel textures with raw vTexCoord or manual UV, as this bypasses the Y correction.
vec2 channelUv(int channelIndex, vec2 fragCoord) {
    vec2 r = max(channelSize(channelIndex), vec2(1.0));
    vec2 uv = fragCoord / r;
    uv.y = 1.0 - uv.y;
    return uv;
}

#endif
