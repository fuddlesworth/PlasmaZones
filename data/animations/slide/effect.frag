// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Slide transition — directional wipe. Direction parameter selects
// the axis: 0=left, 1=right, 2=up, 3=down. iTime drives progress [0, 1].

#version 330 core

uniform float iTime;
uniform vec2 iResolution;
uniform float direction;
uniform float parallax;

in vec2 fragCoord;
out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    float progress = clamp(iTime, 0.0, 1.0);

    int dir = int(clamp(direction, 0.0, 3.0));
    float coord;
    float edge;

    if (dir == 0) {
        coord = uv.x;
        edge = 1.0 - progress;
    } else if (dir == 1) {
        coord = 1.0 - uv.x;
        edge = 1.0 - progress;
    } else if (dir == 2) {
        coord = uv.y;
        edge = 1.0 - progress;
    } else {
        coord = 1.0 - uv.y;
        edge = 1.0 - progress;
    }

    float alpha = step(coord, edge);
    alpha *= (1.0 - parallax * (1.0 - coord) * progress);

    fragColor = vec4(1.0, 1.0, 1.0, clamp(alpha, 0.0, 1.0));
}
