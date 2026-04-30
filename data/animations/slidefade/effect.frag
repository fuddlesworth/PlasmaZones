// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Slide-fade transition — directional reveal of the rendered surface
// (sampled through iChannel0) with a soft alpha gradient at the
// moving edge. Same direction semantics as `slide` (0..3 = left /
// right / up / down) but the leading edge softens via fadeWidth.

#version 450

#include "../_shared/animation_uniforms.glsl"

// metadata.json declaration order → customParams[0] sub-slots.
#define direction customParams[0].x
#define fadeWidth customParams[0].y

layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    float visibility = clamp(qt_Opacity, 0.0, 1.0);

    int dir = int(clamp(direction, 0.0, 3.0));
    float coord;
    if (dir == 0)      coord = uv.x;
    else if (dir == 1) coord = 1.0 - uv.x;
    else if (dir == 2) coord = uv.y;
    else               coord = 1.0 - uv.y;

    float fw = max(fadeWidth, 0.001);
    float alpha = smoothstep(visibility - fw, visibility, coord);
    alpha = 1.0 - alpha; // 1 inside the revealed region, 0 outside

    vec4 sampled = texture(iChannel0, uv);
    fragColor = sampled * alpha;
}
