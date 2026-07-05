// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wobbly-move vertex shader — cursor-anchored jelly on the tessellated
// grid, in the spirit of KDE's wobbly windows. Two motion springs drive
// it (both integrated host-side, no per-vertex state): iMoveVelocity
// tracks the drag tightly, iMoveVelocity2 is deliberately looser and
// rings longer. Each vertex blends between them by its distance from the
// GRIP POINT (the cursor, via iMouse), so the region under the pointer
// follows near-rigidly while far corners trail on the loose spring —
// out of phase with the grip, which is what makes it read as jelly
// rather than a bending sheet. After release both springs ring through
// zero at different rates and the body shimmies to rest.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
uniform vec4 iToRect;
#endif

void main() {
#ifdef PLASMAZONES_KWIN
    // Same card-uv flip as the flow pack (KWin's window-quad texcoord
    // convention is Y-flipped on upload): y = 0 at the window's top.
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    // Grip point: the cursor in card space (iMouse.zw is the normalized
    // window-local position, pushed per frame). Clamped so a pointer that
    // slips outside the frame mid-drag keeps a sane anchor at the edge.
    vec2 grip = clamp(iMouse.zw, 0.0, 1.0);

    // Distance from the grip in logical px (iToRect carries the live frame
    // rect for held transitions), so the falloff is round on any aspect.
    vec2 dpx = (cuv - grip) * iToRect.zw;
    float dist = length(dpx);
    float w = smoothstep(0.0, max(p_reach, 40.0), dist);

    // Near the grip: the tight spring (follows the pointer). Far away: the
    // loose spring (trails and rings longer). The phase difference between
    // the two across the body IS the wobble.
    vec2 v = mix(iMoveVelocity, iMoveVelocity2, w);
    vec2 lag = -v * (p_softness / 1000.0) * (0.12 + 0.88 * w);

    // Cap the deflection so a violent fling cannot fold the window over
    // itself; the clamp preserves direction.
    float m = length(lag);
    float cap = max(float(p_maxBend), 8.0);
    if (m > cap) {
        lag *= cap / m;
    }

    vTexCoord = cuv;
    gl_Position = modelViewProjectionMatrix * vec4(position + lag, 0.0, 1.0);
#else
    // Daemon RHI bake target: the deformation is compositor-only. Pass the
    // quad through so the shader still bakes — mirrors flow's daemon branch.
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
#endif
}
