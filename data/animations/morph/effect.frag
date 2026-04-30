// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Morph transition — sine-based displacement field warps UV space
// while cross-fading. iTime drives progress [0, 1].

#version 450

#include "../_shared/animation_uniforms.glsl"

// metadata.json declaration order → customParams[0] sub-slots
#define warpStrength  customParams[0].x
#define warpFrequency customParams[0].y

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    float progress = clamp(iTime, 0.0, 1.0);

    float envelope = sin(progress * 3.14159);
    float strength = warpStrength * envelope;
    float freq = max(warpFrequency, 1.0);

    vec2 warp = vec2(
        sin(uv.y * freq * 6.28318 + progress * 6.28318) * strength,
        cos(uv.x * freq * 6.28318 + progress * 6.28318) * strength
    );

    vec2 warpedUv = clamp(uv + warp, 0.0, 1.0);
    float alpha = 1.0 - progress;
    float warpMask = 1.0 - length(warp) * 2.0;

    fragColor = vec4(warpedUv.x, warpedUv.y, warpMask, alpha);
}
