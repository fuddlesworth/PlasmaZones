// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Pixelate — the two desktops crossfade while the whole screen quantises
// into pixel blocks that grow toward the midpoint of the switch and shrink back
// to sharp by the end. Ported from GL-Transitions "pixelize" by gre
// (https://github.com/gl-transitions/gl-transitions, MIT). progress -> t;
// p_blocks sets the block density at the sharp ends.
#include <desktop_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
    float d = min(t, 1.0 - t);           // 0 at the ends, 0.5 at the midpoint
    const float steps = 50.0;            // quantise the block size so it steps, not glides
    float dist = ceil(d * steps) / steps;
    vec2 squareSize = 2.0 * dist / vec2(max(p_blocks, 1.0));
    vec2 p = dist > 0.0 ? (floor(uv / squareSize) + 0.5) * squareSize : uv;
    // Clamp the blend factor: iTime can leave [0,1] for an overshooting curve, and
    // crossFade is a plain mix(), so t = 1.2 extrapolates to 1.2*to - 0.2*from —
    // colours below 0 or above 1. A unorm8 target clamps that into an over-contrast
    // flash; a float/HDR target does not clamp it at all. A cross-fade has no
    // meaningful state past its endpoints.
    return crossFade(p, clamp(t, 0.0, 1.0));
}
