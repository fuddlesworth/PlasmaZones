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
#ifdef PLASMAZONES_KWIN
    float d = min(t, 1.0 - t);           // 0 at the ends, 0.5 at the midpoint
    const float steps = 50.0;            // quantise the block size so it steps, not glides
    float dist = ceil(d * steps) / steps;
    vec2 squareSize = 2.0 * dist / vec2(max(p_blocks, 1.0));
    vec2 p = dist > 0.0 ? (floor(uv / squareSize) + 0.5) * squareSize : uv;
    return crossFade(p, t);
#else
    return vec4(0.0);
#endif
}
