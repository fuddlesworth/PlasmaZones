// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glitch transition — block displacement + RGB channel offset.
// iTime drives progress [0, 1].

#version 450

uniform float iTime;
uniform vec2 iResolution;
uniform float intensity;
uniform float blockSize;
uniform float rgbSplit;

in vec2 fragCoord;
out vec4 fragColor;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    float progress = clamp(iTime, 0.0, 1.0);

    float glitchStrength = intensity * sin(progress * 3.14159);

    float bs = max(blockSize, 0.01);
    vec2 block = floor(uv / bs);
    float blockNoise = hash(block + floor(iTime * 10.0));

    float displacement = 0.0;
    if (blockNoise > (1.0 - glitchStrength * 0.5))
        displacement = (hash(block * 2.0) - 0.5) * glitchStrength * 0.2;

    vec2 uvR = uv + vec2(displacement + rgbSplit * glitchStrength, 0.0);
    vec2 uvG = uv + vec2(displacement, 0.0);
    vec2 uvB = uv + vec2(displacement - rgbSplit * glitchStrength, 0.0);

    float r = smoothstep(0.0, 1.0, uvR.x) * smoothstep(0.0, 1.0, uvR.y);
    float g = smoothstep(0.0, 1.0, uvG.x) * smoothstep(0.0, 1.0, uvG.y);
    float b = smoothstep(0.0, 1.0, uvB.x) * smoothstep(0.0, 1.0, uvB.y);
    float alpha = 1.0 - progress;

    fragColor = vec4(r * alpha, g * alpha, b * alpha, alpha);
}
