// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Nexus Cascade — Buffer Pass 2: Bloom/glow combine
// Samples iChannel0 and iChannel1, outputs soft bloom + combine to iChannel2.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 res = max(iResolution.xy, vec2(1.0));
    vec2 uv = fragCoord / res;

    float bloomRadius = customParams[2].x > 0.1 ? customParams[2].x : 3.0;
    float bloomStrength = customParams[2].y > 0.001 ? customParams[2].y : 0.25;
    float blend = customParams[2].z > 0.001 ? customParams[2].z : 0.6;

    vec2 px = 1.0 / res;
    float r = bloomRadius * 0.5;

    // 5-tap cross blur on channel 1 (distorted layer) for glow
    vec4 c1 = texture(iChannel1, channelUv(1, fragCoord));
    vec4 b1 = texture(iChannel1, channelUv(1, fragCoord + vec2(r * px.x, 0.0)));
    vec4 b2 = texture(iChannel1, channelUv(1, fragCoord + vec2(-r * px.x, 0.0)));
    vec4 b3 = texture(iChannel1, channelUv(1, fragCoord + vec2(0.0, r * px.y)));
    vec4 b4 = texture(iChannel1, channelUv(1, fragCoord + vec2(0.0, -r * px.y)));
    vec4 blur = (c1 * 2.0 + b1 + b2 + b3 + b4) / 6.0;

    vec4 ch0 = texture(iChannel0, channelUv(0, fragCoord));

    vec3 combined = mix(ch0.rgb, c1.rgb, blend);
    combined += blur.rgb * bloomStrength;

    fragColor = vec4(clamp(combined, 0.0, 1.0), 1.0);
}
