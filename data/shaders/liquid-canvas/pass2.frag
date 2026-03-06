// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Liquid Canvas -- Buffer Pass 2: Edge-aware bloom and light diffusion
// Reads iChannel0 (flow field) and iChannel1 (distorted texture).
// Applies directional bloom along flow lines + edge glow at high-contrast boundaries.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <audio.glsl>

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 uv = fragCoord / max(iResolution.xy, vec2(1.0));
    vec2 px = 1.0 / max(iResolution.xy, vec2(1.0));

    float bloomRadius = customParams[2].x >= 0.0 ? customParams[2].x : 3.0;
    float bloomStr    = customParams[2].y >= 0.0 ? customParams[2].y : 0.3;
    float edgeGlow    = customParams[2].z >= 0.0 ? customParams[2].z : 0.5;
    float audioReact  = customParams[0].w >= 0.0 ? customParams[0].w : 1.0;

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass     = getBassSoft();
    float treble   = getTrebleSoft();

    // Sample center color from pass 1
    vec2 ch1Uv = channelUv(1, fragCoord);
    vec4 center = texture(iChannel1, ch1Uv);

    // Decode flow direction from pass 0 for directional blur
    vec4 flowData = texture(iChannel0, channelUv(0, fragCoord));
    vec2 rawFlow = flowData.rg * 2.0 - 1.0;
    vec2 flowDir = length(rawFlow) > 0.001 ? normalize(rawFlow) : vec2(0.0);
    float flowMag = flowData.b * 2.0;

    // -- Directional bloom along flow lines --
    // Blur samples stretched along the flow direction, creating motion-like bloom.
    // Use uniform pixel step (average of x/y) so bloom strength is consistent
    // regardless of aspect ratio (prevents 56% weaker horizontal on widescreen).
    float pxUniform = (px.x + px.y) * 0.5;
    vec3 bloom = vec3(0.0);
    float totalWeight = 0.0;
    int samples = 7;
    for (int i = -samples; i <= samples; i++) {
        float fi = float(i);
        float w = exp(-fi * fi / (bloomRadius * bloomRadius * 0.5));
        vec2 offset = flowDir * fi * pxUniform * bloomRadius;
        bloom += texture(iChannel1, ch1Uv + offset).rgb * w;
        totalWeight += w;
    }
    bloom /= totalWeight;

    // -- Edge detection (Sobel-like) for edge glow --
    float lumC = luminance(center.rgb);
    float lumL = luminance(texture(iChannel1, ch1Uv + vec2(-px.x, 0.0)).rgb);
    float lumR = luminance(texture(iChannel1, ch1Uv + vec2(px.x, 0.0)).rgb);
    float lumU = luminance(texture(iChannel1, ch1Uv + vec2(0.0, -px.y)).rgb);
    float lumD = luminance(texture(iChannel1, ch1Uv + vec2(0.0, px.y)).rgb);
    float edge = abs(lumR - lumL) + abs(lumD - lumU);
    edge = smoothstep(0.02, 0.2, edge);

    // Edge glow color from palette
    vec3 edgeColor = colorWithFallback(customColors[2].rgb, vec3(0.4, 0.8, 0.73));

    // Combine: base + bloom overlay + edge highlights
    vec3 col = center.rgb;

    // Add bloom (soft light blend)
    col = mix(col, bloom, bloomStr * (0.5 + flowMag * 0.5));

    // Add edge glow
    col += edgeColor * edge * edgeGlow;

    // -- Treble: sparkle highlights at edges --
    // Treble creates bright sparkle points along detected edges,
    // like light catching the ridges of brushstrokes.
    if (hasAudio && treble > 0.06) {
        float sparkle = noise2D(uv * 60.0 + iTime * 3.0);
        sparkle = smoothstep(0.7, 0.95, sparkle);
        col += edgeColor * sparkle * edge * treble * audioReact * 1.5;
    }

    // -- Bass: bloom intensity pulse --
    if (hasAudio && bass > 0.04) {
        col = mix(col, bloom, bass * audioReact * 0.2);
    }

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
