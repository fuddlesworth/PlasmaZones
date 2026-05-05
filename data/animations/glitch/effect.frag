// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glitch transition — operates on the rendered surface (iChannel0,
// binding 7) by sampling the captured surface with per-block UV
// displacement and per-channel RGB offset. The previous stub built
// `r/g/b` from `smoothstep(0, 1, uv)` (a centred radial mask) —
// visually a dim white circle, not a glitch. This version reads the
// surface directly and produces the displaced-channel chromatic
// aberration the metadata advertises. `iTime` is the per-leg [0,1]
// progress driven by SurfaceAnimator's shaderTime AnimatedValue —
// `sin(iTime*pi)` peaks the glitch mid-transition and settles at
// both endpoints regardless of direction.

#version 450

#include <animation_uniforms.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define intensity customParams[0].x
#define blockSize customParams[0].y
#define rgbSplit  customParams[0].z

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    // UV from the vertex stage; gl_FragCoord/iResolution overshoots [0,1]
    // by DPR on high-DPI displays.
    vec2 uv = vTexCoord;

    // Glitch peaks mid-leg (sin shape) regardless of direction so both
    // show and hide get the same "rip apart, then settle / settle, then
    // rip apart" feel without the shader needing to know the leg sign.
    // iTime is the per-leg [0,1] progress: iTime 0 → strength 0
    // (start of leg), iTime 1 → strength 0 (end of leg), iTime 0.5 →
    // peak.
    float visibility = clamp(iTime, 0.0, 1.0);
    float strength = intensity * sin(visibility * 3.14159);

    float bs = max(blockSize, 0.01);
    vec2 block = floor(uv / bs);
    // floor(iTime * 10.0) quantises the jitter to ~10 buckets across the
    // [0,1] leg — at 60Hz playback that's a fresh displacement every ~6
    // frames, giving a step-frame "glitch" feel rather than continuous
    // smooth noise. Replace `floor(iTime * 10.0)` with `iTime * 10.0` for
    // continuous noise if a smoother variant is wanted.
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
    // Qt Quick uses premultiplied-alpha blending, so un-premultiply
    // each sample before extracting the single channel, then
    // re-premultiply against the chosen alpha.
    vec4 sR = texture(iChannel0, uvR);
    vec4 sG = texture(iChannel0, uvG);
    vec4 sB = texture(iChannel0, uvB);
    float a = sG.a;
    float r = (sR.a > 0.001) ? sR.r / sR.a : 0.0;
    float g = (sG.a > 0.001) ? sG.g / sG.a : 0.0;
    float b = (sB.a > 0.001) ? sB.b / sB.a : 0.0;
    fragColor = vec4(r * a, g * a, b * a, a);
}
