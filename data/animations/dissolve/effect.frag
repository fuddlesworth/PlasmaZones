// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Dissolve transition — noise-based alpha fade between two states.
// iTime drives progress [0, 1]. Grain parameter controls noise frequency.

#version 330 core

uniform float iTime;
uniform vec2 iResolution;
uniform float grain;    // noise cell size
uniform float softness; // edge softness

in vec2 fragCoord;
out vec4 fragColor;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    float cellSize = max(grain, 0.01);
    vec2 cell = floor(uv / cellSize);
    float noise = hash(cell);

    float progress = clamp(iTime, 0.0, 1.0);
    float soft = max(softness, 0.001);
    float alpha = smoothstep(progress - soft, progress + soft, noise);

    fragColor = vec4(1.0, 1.0, 1.0, alpha);
}
