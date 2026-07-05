// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wobbly-move vertex shader — delayed path-following jelly, entirely in
// the shader. What a spring mesh (KDE's wobbly windows) visually does is
// simple to state: the region under the pointer follows the drag NOW,
// and regions farther away follow the path the window took a moment AGO,
// smoothed and with a little ring after release. iMoveTrail hands this
// stage exactly that — the window's origin up to 240 ms into the past,
// relative to its current origin — so each vertex just samples the trail
// at a delay proportional to its distance from the grip (iMouse). Drag
// in an arc and the body sweeps through the arc behind the pointer;
// stop, and the trail drains toward zero so the body glides back in.
// iMoveVelocity2 (the deliberately underdamped host spring) adds the
// overshoot ring on release. No per-vertex state anywhere.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
uniform vec4 iToRect;

// Trail sample at a continuous delay in ms (linear blend between the two
// bracketing 15 ms slots; slot "-1" is the present = zero offset).
vec2 trailAt(float delayMs) {
    float fi = clamp(delayMs / 15.0, 0.0, 15.999);
    int i1 = int(fi);
    float f = fi - float(i1);
    vec2 s0 = (i1 == 0) ? vec2(0.0) : iMoveTrail[i1 - 1];
    vec2 s1 = iMoveTrail[i1];
    return mix(s0, s1, f);
}
#endif

void main() {
#ifdef PLASMAZONES_KWIN
    // Same card-uv flip as the flow pack (KWin's window-quad texcoord
    // convention is Y-flipped on upload): y = 0 at the window's top.
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    // Grip point: the cursor in card space, clamped so a pointer that
    // slips outside the frame mid-drag keeps a sane anchor at the edge.
    vec2 grip = clamp(iMouse.zw, 0.0, 1.0);

    // Per-vertex delay: zero at the grip (follows the pointer rigidly),
    // rising to p_lag ms at p_reach px away — the far side of the window
    // replays the drag path late, which IS the wobble.
    vec2 dpx = (cuv - grip) * iToRect.zw;
    float w = smoothstep(0.0, max(p_reach, 40.0), length(dpx));
    vec2 lag = trailAt(w * clamp(p_lag, 0.0, 235.0));

    // Release ring: the underdamped host spring crosses zero after the
    // pointer stops, so the body overshoots once and settles.
    lag += -iMoveVelocity2 * (p_ring / 1000.0) * w;

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
