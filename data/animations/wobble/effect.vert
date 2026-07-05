// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wobbly-move vertex shader — velocity-lag jelly deformation on the flow
// pack's tessellated grid, driven by the HELD-transition motion contract
// rather than the clock. iMoveVelocity is the spring-smoothed window
// velocity (logical px/s): while dragging, each grid vertex trails
// opposite the motion by `softness` milliseconds of travel, weighted so
// the grip edge (top, where the pointer usually holds the title bar)
// follows tightly and the far edge lags and bends. Because the published
// velocity is deliberately underdamped C++-side, it rings through zero
// after the pointer stops or releases — the trailing body overshoots and
// settles like jelly with NO per-vertex state in the shader.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
#endif

void main() {
#ifdef PLASMAZONES_KWIN
    // Same card-uv flip as the flow pack (KWin's window-quad texcoord
    // convention is Y-flipped on upload): y = 0 at the window's top.
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    // Lag = velocity * softness, opposite the motion. Quadratic weight in
    // the card's vertical: the grip edge moves near-rigidly, the bottom
    // sweeps — the classic dragged-sheet curvature. A slight horizontal
    // cup (edges lag a touch more than the centre column) keeps the body
    // reading as soft rather than sheared.
    float follow = clamp(p_grip, 0.0, 0.95);
    float wy = mix(follow, 1.0, cuv.y * cuv.y);
    float cup = 1.0 + 0.25 * (abs(cuv.x - 0.5) * 2.0) * (abs(cuv.x - 0.5) * 2.0);
    vec2 lag = -iMoveVelocity * (p_softness / 1000.0) * wy * cup;

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
