// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Slide transition — directional reveal of the rendered surface.
// `direction` selects the axis: 0=left, 1=right, 2=up, 3=down. The
// surface is sampled through iChannel0; we mask it with a moving
// edge so it slides into / out of view from the chosen direction.

#version 450

#include <animation_uniforms.glsl>

// metadata.json declaration order → customParams[0] sub-slots.
// `direction` is metadata-typed `int`; the registry packs ints into
// the same float slot space, so we read it as a float and round at
// the use site.
#define direction customParams[0].x
#define parallax  customParams[0].y

layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    // UV from the vertex stage; gl_FragCoord/iResolution overshoots [0,1]
    // by DPR on high-DPI displays.
    vec2 uv = vTexCoord;

    // Visibility = how revealed the surface is. iTime is per-leg
    // progress: SurfaceAnimator runs iTime 0→1 on show and 1→0 on
    // hide, so the reveal mask grows on show ("slide in") and
    // recedes on hide ("slide out") through the same code path.
    float visibility = clamp(iTime, 0.0, 1.0);

    int dir = int(clamp(direction, 0.0, 3.0));
    float coord;
    if (dir == 0) {
        coord = uv.x;            // slide in from the left
    } else if (dir == 1) {
        coord = 1.0 - uv.x;      // slide in from the right
    } else if (dir == 2) {
        coord = uv.y;            // slide in from the top
    } else {
        coord = 1.0 - uv.y;      // slide in from the bottom
    }

    // Reveal mask: pixels where coord < visibility are shown. With
    // `parallax` > 0, sample with a small per-row offset so the
    // surface appears to "slip" as it slides.
    if (coord > visibility) {
        fragColor = vec4(0.0);
        return;
    }
    vec2 sampleUv = uv;
    if (parallax > 0.0) {
        float offset = parallax * (visibility - coord);
        if (dir == 0)      sampleUv.x += offset;
        else if (dir == 1) sampleUv.x -= offset;
        else if (dir == 2) sampleUv.y += offset;
        else               sampleUv.y -= offset;
    }
    fragColor = texture(iChannel0, clamp(sampleUv, 0.0, 1.0));
}
