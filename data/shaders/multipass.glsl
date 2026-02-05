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

// UV for iChannel i when resolution differs from iResolution. channelIndex 0–3.
vec2 channelUv(int channelIndex, vec2 fragCoord) {
    vec2 r = max(iChannelResolution[channelIndex], vec2(1.0));
    return fragCoord / r;
}

#endif
