// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pixel Wipe — pixelated radial dissolve. A pixelation+dissolution
// wavefront emanates from a configurable origin point, with the
// transition zone showing chunky pixels noise-keyed for randomness.
// Visually inspired by Burn-My-Windows (pixel-wipe.frag, Simon
// Schneegans), written natively against our `iTime`/`iChannel0`.
//
// ## iTime convention
//
// `bmwProgress = easeOutQuad(iTime)` — same direction as BMW open
// (window forms from origin outward). On show, the solid region
// expands from the origin point. On hide, the dissolved region
// contracts toward the origin.
//
// We chose Format-A (forward-mapped iTime → bmwProgress) over
// Format-B (`bmwProgress = easeOutQuad(1-iTime)`, which would
// match BMW close on hide instead). With Format-A the show is
// the more conventional "window materializes from a point and
// expands" reading; the hide reads as "window collapses inward
// to a point", which is also natural. Either formulation has to
// invert one direction since the spatial wavefront can only
// move outward in one direction of a single function-of-iTime.

#version 450

#include <animation_uniforms.glsl>

#define maxPixelSize customParams[0].x
#define originX      customParams[0].y
#define originY      customParams[0].z

layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float FADE_WIDTH = 1.0;

float easeOutQuad(float x) { return -1.0 * x * (x - 2.0); }

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}
float simplex2D(vec2 p) {
    const float K1 = 0.366025404;
    const float K2 = 0.211324865;
    vec2 i  = floor(p + (p.x + p.y) * K1);
    vec2 a  = p - i + (i.x + i.y) * K2;
    float m = step(a.y, a.x);
    vec2 o  = vec2(m, 1.0 - m);
    vec2 b  = a - o + K2;
    vec2 c  = a - 1.0 + 2.0 * K2;
    vec3 h  = max(0.5 - vec3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    vec3 n  = h * h * h * h *
            vec3(dot(a, -1.0 + 2.0 * hash22(i + 0.0)),
                 dot(b, -1.0 + 2.0 * hash22(i + o)),
                 dot(c, -1.0 + 2.0 * hash22(i + 1.0)));
    return 0.5 + 0.5 * dot(n, vec3(70.0));
}

// 4-octave fractal simplex for the dissolve threshold randomness.
// Heavier than single-octave but gives the chunky "burn pattern"
// rather than a smooth gradient.
float simplex2DFractal(vec2 p) {
    mat2 m  = mat2(1.6, 1.2, -1.2, 1.6);
    float f = 0.5000 * simplex2D(p);  p = m * p;
    f      += 0.2500 * simplex2D(p);  p = m * p;
    f      += 0.1250 * simplex2D(p);  p = m * p;
    f      += 0.0625 * simplex2D(p);
    return f;
}

void main()
{
    vec2 uv = vTexCoord;
    vec2 origin = vec2(originX, originY);

    // Per-pixel distance from the origin. Window-corner case: max
    // distance is sqrt(2) ≈ 1.414 (diagonal of unit square).
    float circle = length(uv - origin);

    // Same formula as BMW open direction: solid region expands from
    // origin outward as bmwProgress goes 0→1 (which corresponds to
    // iTime going 0→1, our show direction).
    float bmwProgress = easeOutQuad(clamp(iTime, 0.0, 1.0));
    float gradient    = ((1.0 - bmwProgress) * (1.0 + FADE_WIDTH) - 1.0 + circle) / FADE_WIDTH;
    float dissolve    = smoothstep(0.0, 1.0, gradient);

    // Pixelation grows with the per-pixel dissolve amount, so cells
    // currently in the transition wave are chunkily pixelated while
    // already-solid (dissolve→0) and already-gone (dissolve→1)
    // regions stay at their natural pixel size.
    float pixelSize = ceil(maxPixelSize * dissolve + 1.0);
    vec2 pixelGrid  = vec2(pixelSize) / iResolution;
    vec2 cellUV     = uv - mod(uv, pixelGrid) + pixelGrid * 0.5;
    vec4 sampled    = texture(iChannel0, cellUV);

    // Per-cell noise threshold gates the dissolve so the wavefront
    // is a chunky speckled pattern rather than a smooth ring. A cell
    // becomes transparent when dissolve > random_for_this_cell, with
    // a soft falloff over `1/10` of the dissolve range.
    float random = simplex2DFractal(cellUV * iResolution / 20.0) * 1.5 - 0.25;
    if (dissolve > random) {
        sampled *= max(0.0, 1.0 - (dissolve - random) * 10.0);
    }

    fragColor = sampled;
}
