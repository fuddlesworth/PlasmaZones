// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pop-in transition — scale from center with optional overshoot.
// iTime drives progress [0, 1]. The fragment shader outputs a mask;
// actual scaling is handled by the compositor adapter.

#version 330 core

uniform float iTime;
uniform vec2 iResolution;
uniform float scaleFrom;
uniform float overshoot;

in vec2 fragCoord;
out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    float progress = clamp(iTime, 0.0, 1.0);

    float scale = mix(scaleFrom, 1.0 + overshoot, progress);
    if (progress > 0.7)
        scale = mix(1.0 + overshoot, 1.0, (progress - 0.7) / 0.3);

    vec2 center = vec2(0.5);
    vec2 scaledUv = (uv - center) / max(scale, 0.001) + center;

    float inBounds = step(0.0, scaledUv.x) * step(scaledUv.x, 1.0) *
                     step(0.0, scaledUv.y) * step(scaledUv.y, 1.0);

    float alpha = progress * inBounds;

    fragColor = vec4(1.0, 1.0, 1.0, alpha);
}
