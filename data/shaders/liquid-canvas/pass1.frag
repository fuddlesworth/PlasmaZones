// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Liquid Canvas -- Buffer Pass 1: Texture sampling with flow distortion
// Reads iChannel0 (flow field from pass 0) and the desktop wallpaper.
// Image source: uWallpaper (desktop wallpaper) > procedural watercolor fallback.
// Applies flow-based UV displacement + chromatic aberration + paint blending.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <wallpaper.glsl>
#include <audio.glsl>

// Procedural watercolor pattern used when no image source is available
vec3 watercolorFallback(vec2 uv, float t) {
    vec3 c1 = colorWithFallback(customColors[0].rgb, vec3(0.27, 0.53, 0.8));
    vec3 c2 = colorWithFallback(customColors[1].rgb, vec3(0.8, 0.4, 0.67));
    vec3 c3 = colorWithFallback(customColors[2].rgb, vec3(0.4, 0.8, 0.73));

    // Layered noise creates watercolor-like organic color fields
    float n1 = noise2D(uv * 4.0 + t * 0.3);
    float n2 = noise2D(uv * 7.0 - t * 0.2 + 100.0);
    float n3 = noise2D(uv * 11.0 + t * 0.15 + 200.0);

    vec3 col = mix(c1, c2, smoothstep(0.3, 0.7, n1));
    col = mix(col, c3, smoothstep(0.4, 0.6, n2) * 0.6);

    // Wet-edge darkening at noise boundaries
    float edge = abs(n1 - 0.5) + abs(n2 - 0.5);
    col *= 0.7 + 0.3 * smoothstep(0.1, 0.5, edge);

    // Pigment granulation: tiny dark speckles
    float grain = noise2D(uv * 80.0 + t);
    col *= 0.92 + 0.08 * grain;

    return col;
}

// Sample an image texture with aspect-correct UV and flow distortion
vec3 sampleImage(sampler2D img, vec2 uv, vec2 displacement,
                 vec2 flowDir, float chromaSplit, float paintBlend, float flowMag,
                 bool hasAudio, float mids, float audioReact) {
    // Aspect-correct UV via shared helper (wallpaper.glsl)
    vec2 texUv = wallpaperUv(uv * iResolution.xy, iResolution.xy);

    // Apply flow displacement and clamp to prevent edge stretching
    vec2 distortedUv = clamp(texUv + displacement, 0.0, 1.0);

    // Chromatic aberration: sample RGB at slightly offset positions along flow
    vec2 chromaOffset = flowDir * chromaSplit;
    if (hasAudio && mids > 0.05) {
        chromaOffset *= 1.0 + mids * audioReact;
    }

    float r = texture(img, clamp(distortedUv + chromaOffset, 0.0, 1.0)).r;
    float g = texture(img, distortedUv).g;
    float b = texture(img, clamp(distortedUv - chromaOffset, 0.0, 1.0)).b;
    vec3 col = vec3(r, g, b);

    // Paint-like color smearing: blend with neighboring flow-shifted samples
    if (paintBlend > 0.01) {
        vec2 smearUv1 = clamp(texUv + displacement * 1.5 + vec2(0.003, 0.0), 0.0, 1.0);
        vec2 smearUv2 = clamp(texUv + displacement * 0.5 - vec2(0.0, 0.003), 0.0, 1.0);
        vec3 smear1 = texture(img, smearUv1).rgb;
        vec3 smear2 = texture(img, smearUv2).rgb;
        col = mix(col, (smear1 + smear2) * 0.5, paintBlend * flowMag);
    }

    return col;
}

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 uv = fragCoord / max(iResolution.xy, vec2(1.0));

    float distortStr  = customParams[1].x >= 0.0 ? customParams[1].x : 0.04;
    float paintBlend  = customParams[1].y >= 0.0 ? customParams[1].y : 0.3;
    float chromaSplit = customParams[1].z >= 0.0 ? customParams[1].z : 0.008;
    float fallbackMix = customParams[1].w >= 0.0 ? customParams[1].w : 1.0;
    float audioReact  = customParams[0].w >= 0.0 ? customParams[0].w : 1.0;

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass     = getBassSoft();
    float mids     = getMidsSoft();

    // Decode flow field from pass 0
    vec4 flowData = texture(iChannel0, channelUv(0, fragCoord));
    vec2 flowDir = flowData.rg * 2.0 - 1.0; // decode from [0,1] to [-1,1]
    float flowMag = flowData.b * 2.0;        // decode magnitude

    // Displacement UV offset from flow field
    vec2 displacement = flowDir * flowMag * distortStr;

    // Bass amplifies displacement in bursts
    if (hasAudio && bass > 0.05) {
        displacement *= 1.0 + bass * audioReact * 0.7;
    }

    // Image source: desktop wallpaper > procedural watercolor
    vec2 wpSize = vec2(textureSize(uWallpaper, 0));
    bool hasWallpaper = wpSize.x > 1.0 && wpSize.y > 1.0;

    vec3 col;

    if (hasWallpaper) {
        // Desktop wallpaper as canvas source
        col = sampleImage(uWallpaper, uv, displacement,
                          flowDir, chromaSplit, paintBlend, flowMag,
                          hasAudio, mids, audioReact);
    } else {
        // Procedural watercolor fallback
        vec2 distortedUv = uv + displacement;
        col = watercolorFallback(distortedUv, iTime) * fallbackMix;

        // Chromatic shift for visual interest (single offset sample, split channels)
        vec2 chromaOffset = flowDir * chromaSplit * 2.0;
        vec3 shifted = watercolorFallback(distortedUv + chromaOffset, iTime);
        col = vec3(shifted.r, col.g, shifted.b);
    }

    // Flow-intensity-based brightness modulation (brighter where flow converges)
    float flowBright = 0.85 + 0.15 * flowMag;
    col *= flowBright;

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
