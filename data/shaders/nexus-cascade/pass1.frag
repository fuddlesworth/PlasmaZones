// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Nexus Cascade — Buffer Pass 1: Distortion + overlay layer
// Samples iChannel0, outputs to iChannel1.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 uv = fragCoord / max(iResolution.xy, vec2(1.0));

    float distortStrength = customParams[1].x >= 0.0 ? customParams[1].x : 0.03;
    float rippleFreq = customParams[1].y >= 0.0 ? customParams[1].y : 8.0;
    float scanlineOpacity = customParams[1].z >= 0.0 ? customParams[1].z : 0.0;
    float t = iTime * (customParams[0].x >= 0.0 ? customParams[0].x : 0.4);

    vec2 centered = uv - 0.5;
    float angle = atan(centered.y, centered.x);
    float r = length(centered);

    float swirlPhase = angle + t * 1.8 + r * rippleFreq * 0.5;
    vec2 swirl = vec2(cos(swirlPhase), sin(swirlPhase)) * distortStrength * (0.7 + 0.3 * sin(r * 6.0 - t * 2.0));
    vec2 ripple = vec2(sin(uv.y * rippleFreq + t * 2.0), cos(uv.x * rippleFreq - t * 1.5)) * distortStrength * 0.5;
    vec2 distort = swirl + ripple;

    vec2 sampleUV = channelUv(0, fragCoord) + distort;

    vec4 base = texture(iChannel0, sampleUV);

    // Subtle scanline overlay (optional)
    float scan = 1.0;
    if (scanlineOpacity > 0.001) {
        float scanY = fract(uv.y * 0.5 * iResolution.y);
        scan = 1.0 - smoothstep(0.0, 0.5, scanY) * scanlineOpacity * 0.15;
    }
    base.rgb *= scan;

    float vig = 1.0 - 0.06 * length(uv - 0.5);
    base.rgb *= vig;

    fragColor = vec4(clamp(base.rgb, 0.0, 1.0), 1.0);
}
