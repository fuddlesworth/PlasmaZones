// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glitch transition — operates on the rendered surface (iChannel0,
// binding 7) by sampling the captured surface with per-block UV
// displacement and per-channel RGB offset. The previous stub built
// `r/g/b` from `smoothstep(0, 1, uv)` (a centred radial mask) —
// visually a dim white circle, not a glitch. This version reads the
// surface directly and produces the displaced-channel chromatic
// aberration the metadata advertises.

#version 450

#include "../_shared/animation_uniforms.glsl"

// metadata.json declaration order → customParams[0] sub-slots
#define intensity customParams[0].x
#define blockSize customParams[0].y
#define rgbSplit  customParams[0].z

layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) out vec4 fragColor;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;

    // Glitch peaks mid-leg (sin shape) regardless of direction so both
    // show and hide get the same "rip apart, then settle / settle, then
    // rip apart" feel without the shader needing to know the leg sign.
    // qt_Opacity 0 → strength 0 (settled, fully hidden), qt_Opacity
    // 1 → strength 0 (settled, fully visible), qt_Opacity 0.5 → peak.
    float visibility = clamp(qt_Opacity, 0.0, 1.0);
    float strength = intensity * sin(visibility * 3.14159);

    float bs = max(blockSize, 0.01);
    vec2 block = floor(uv / bs);
    // iTime is still useful here for the per-frame noise advance —
    // without it every block would pick the same displacement and
    // the glitch would freeze instead of jitter.
    float blockNoise = hash(block + floor(iTime * 10.0));

    float displacement = 0.0;
    if (blockNoise > (1.0 - strength * 0.5)) {
        displacement = (hash(block * 2.0) - 0.5) * strength * 0.2;
    }

    vec2 uvR = uv + vec2(displacement + rgbSplit * strength, 0.0);
    vec2 uvG = uv + vec2(displacement, 0.0);
    vec2 uvB = uv + vec2(displacement - rgbSplit * strength, 0.0);

    // Sample the surface with per-channel offset for the chromatic
    // aberration. Texture sampler has clampToEdge so off-surface UVs
    // bleed edge colour rather than wrapping or going transparent.
    float r = texture(iChannel0, uvR).r;
    float g = texture(iChannel0, uvG).g;
    float b = texture(iChannel0, uvB).b;
    float a = texture(iChannel0, uvG).a;

    fragColor = vec4(r, g, b, a);
}
