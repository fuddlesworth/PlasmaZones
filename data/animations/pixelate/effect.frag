// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pixelate transition — progressively increases pixel/block size while
// fading. iTime drives progress [0, 1].

#version 450

uniform float iTime;
uniform vec2 iResolution;
uniform float maxBlockSize;

in vec2 fragCoord;
out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    float progress = clamp(iTime, 0.0, 1.0);

    float blockPx = max(maxBlockSize * progress, 1.0 / max(iResolution.x, 1.0));
    vec2 cell = floor(uv / blockPx) * blockPx + blockPx * 0.5;

    float alpha = 1.0 - progress;

    fragColor = vec4(cell.x, cell.y, 1.0, alpha);
}
