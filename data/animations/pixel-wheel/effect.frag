// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pixel Wheel — pixelated clockwise spoke dissolve. Pixels are
// snapped to a grid whose cell size grows with progress; each cell
// is gated by a per-spoke threshold so the wheel "spins" as it
// dissolves. Visually inspired by Burn-My-Windows (pixel-wheel.frag,
// Simon Schneegans), written natively against our `iTime`/`iChannel0`.
//
// ## iTime convention
//
// `progress = smoothstep(0, 1, 1 - iTime)` — 0 at visible, 1 at
// destroyed. The internal smoothstep softens the wheel-spin
// endpoints (no abrupt visible→spinning transition). On show
// (iTime 0→1) cells reform spoke-by-spoke; on hide (iTime 1→0)
// cells disappear spoke-by-spoke. Symmetric, no direction branch.
//
// ## Spoke parameterisation
//
// `fragDir = normalize(cellUV - 0.5)` is the unit direction from
// the window centre to the cell. The angle to the "down" direction
// (0,1) gives `[0, 0.5]` going clockwise on the right hemisphere
// and `[0.5, 1]` going counter-clockwise on the left hemisphere
// (after the manual reflection). Multiplying by `spokeCount` and
// taking `mod 1` divides each spoke wedge into its own [0,1]
// gradient. A cell is hidden when `progress > threshold` — so the
// wheel "fills in" each spoke clockwise as progress grows, with
// `spokeCount` parallel sweeps.

#version 450

#include <animation_uniforms.glsl>

#define maxPixelSize customParams[0].x
#define spokeCount   customParams[0].y

layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = vTexCoord;
    float visibility = clamp(iTime, 0.0, 1.0);
    float progress   = smoothstep(0.0, 1.0, 1.0 - visibility);

    // Pixelate. Cell size grows with progress; at progress=0 this is
    // `ceil(0+1)=1` so the grid collapses to per-pixel sampling
    // (visual no-op).
    float pixelSize = ceil(maxPixelSize * progress + 1.0);
    vec2 pixelGrid  = vec2(pixelSize) / iResolution;
    vec2 cellUV     = uv - mod(uv, pixelGrid) + pixelGrid * 0.5;
    vec4 sampled    = texture(iChannel0, cellUV);

    // Spoke parameterisation. `down = (0, 1)`; `dot(down, fragDir)`
    // is just `fragDir.y`. `acos(y)` ∈ [0, π], divided by 2π gives
    // [0, 0.5] for the right half. Manually reflect for left.
    vec2 fragDir = normalize(cellUV - 0.5);
    float angle  = 0.5 * acos(clamp(fragDir.y, -1.0, 1.0)) / 3.14159265359;
    if (fragDir.x < 0.0) {
        angle = 1.0 - angle;
    }

    // Each spoke wedge is its own [0,1] gradient. Cells with low
    // threshold (clockwise-leading edge) are hidden first; cells
    // with high threshold (clockwise-trailing edge) are hidden last.
    float threshold = mod(angle * max(spokeCount, 1.0), 1.0);
    if (progress > threshold) {
        sampled = vec4(0.0);
    }

    fragColor = sampled;
}
