// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Slide-fade transition — directional wipe with a soft alpha gradient
// at the leading edge. iTime drives progress [0, 1].

#version 330 core

uniform float iTime;
uniform vec2 iResolution;
uniform float direction;
uniform float fadeWidth;

in vec2 fragCoord;
out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    float progress = clamp(iTime, 0.0, 1.0);

    int dir = int(clamp(direction, 0.0, 3.0));
    float coord;
    if (dir == 0)
        coord = uv.x;
    else if (dir == 1)
        coord = 1.0 - uv.x;
    else if (dir == 2)
        coord = uv.y;
    else
        coord = 1.0 - uv.y;

    float edge = 1.0 - progress;
    float fw = max(fadeWidth, 0.001);
    float alpha = smoothstep(edge - fw, edge, coord);
    alpha = 1.0 - alpha;

    alpha *= (1.0 - progress * 0.3);

    fragColor = vec4(1.0, 1.0, 1.0, clamp(alpha, 0.0, 1.0));
}
