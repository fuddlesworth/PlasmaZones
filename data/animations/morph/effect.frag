// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Morph transition — sine-based UV displacement field warps the
// rendered surface (sampled through iChannel0). The previous stub
// emitted `vec4(warpedUv.x, warpedUv.y, warpMask, alpha)` — a
// rainbow-like gradient of the UV coords — instead of sampling
// anything, which is the "weird gradient that shows then QML
// renders" report. Now: sample iChannel0 at the warped UV so the
// surface ITSELF deforms during the transition, then settles back
// to its un-warped self as `qt_Opacity` reaches 1.

#version 450

#include "../_shared/animation_uniforms.glsl"

// metadata.json declaration order → customParams[0] sub-slots
#define warpStrength  customParams[0].x
#define warpFrequency customParams[0].y

layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;

    // Envelope peaks at qt_Opacity == 0.5 (mid-transition) and
    // returns to 0 at the endpoints — same shape as glitch, gives
    // both show and hide a "warp peak then settle" feel without
    // needing the leg sign.
    float visibility = clamp(qt_Opacity, 0.0, 1.0);
    float envelope = sin(visibility * 3.14159);
    float strength = warpStrength * envelope;
    float freq = max(warpFrequency, 1.0);

    vec2 warp = vec2(
        sin(uv.y * freq * 6.28318 + iTime * 6.28318) * strength,
        cos(uv.x * freq * 6.28318 + iTime * 6.28318) * strength
    );

    vec2 warpedUv = clamp(uv + warp, 0.0, 1.0);
    fragColor = texture(iChannel0, warpedUv);
}
