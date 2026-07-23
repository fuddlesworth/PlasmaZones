// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Stream vertex shader — the Phosphor set's geometry pack: the
// window pours into its zone as separated luminous streams.
//
// Built on flow's grid mechanics (the grid sits on the padded composite
// canvas with DESTINATION-frame-relative texcoords;
// each vertex's displacement is the pull back toward iFromRect, vanishing
// as its region arrives) with one addition: LANES. The card is cut into
// strips parallel to the travel axis, and each lane runs its own slightly
// offset clock and bows perpendicular to the motion mid-flight — so the
// window separates into streams while it travels and reunites seamlessly
// at both endpoints (the lag and the bow are both exactly zero at e = 0
// and e = 1, so the settled window is placed exactly where KWin put it).
//
// legProgress() keeps the pour direction correct on reverse legs: geometry
// legs always play forward, direction lives in iFromRect / iToRect.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

uniform mat4 modelViewProjectionMatrix;
// Geometry-morph endpoints (logical-screen px, x/y/w/h), pushed by the
// kwin-effect paint pipeline for any shader that declares them.
uniform vec4 iFromRect;
uniform vec4 iToRect;
// Per-vertex flow handed to the fragment: .xy = card uv, .z = arrival
// ease (0 = still at the old rect, 1 = settled), .w = lane seed.
layout(location = 1) out vec4 vFlow;

// Deliberately local rather than including noise.glsl: the vertex stage
// needs exactly one scalar hash, and the frag already carries the full
// noise module for its own use.
float laneHash(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

void main() {
    // Card uv with y = 0 at the window top (KWin Y-flips window-quad
    // texcoords on upload; re-apply the flip, same as flow).
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    // Travel direction in screen space (y-down). A degenerate move (pure
    // resize) defaults to downward so it still reads as a gentle settle.
    vec2 fromC = iFromRect.xy + 0.5 * iFromRect.zw;
    vec2 toC = iToRect.xy + 0.5 * iToRect.zw;
    vec2 travel = toC - fromC;
    vec2 dir = (dot(travel, travel) > 1.0) ? normalize(travel) : vec2(0.0, 1.0);
    vec2 perp = vec2(-dir.y, dir.x);

    // Phase along the travel axis: leading edge (toward the destination)
    // is 1, trailing edge is 0.
    float phase = clamp(dot(cuv - 0.5, dir) + 0.5, 0.0, 1.0);

    // Lane identity: strips parallel to the travel axis. The hash offsets
    // each lane's clock and bow so the streams read as individual pours.
    float laneCount = clamp(p_lanes, 2.0, 24.0);
    float lane = floor(dot(cuv - 0.5, perp) * laneCount + 0.5);
    float lh = laneHash(lane);

    // Staggered local progress: trailing rows lag by `spread` (flow), and
    // each lane adds its own hash lag on top. The lane lag is scaled by the
    // headroom LEFT OVER after `spread`, so spread + laneLag can never reach
    // 1.0 — the denominator stays real and every fragment reaches e = 1 by
    // the leg's end. Without the headroom scaling, spread = 0.8 with full
    // stream stagger pushed startT past 1 for high-hash lanes and those
    // strips froze at the OLD rect when the animation ended, popping to the
    // destination at teardown.
    float spread = clamp(p_spread, 0.0, 0.8);
    float laneLag = clamp(p_streamLag, 0.0, 1.0) * (1.0 - spread) * 0.4;
    float tt = legProgress();
    float startT = (1.0 - phase) * spread + lh * laneLag;
    float localT = clamp((tt - startT) / max(1.0 - spread - laneLag, 1.0e-3), 0.0, 1.0);
    float e = localT * localT * (3.0 - 2.0 * localT);

    // Pull back toward the old rect, vanishing on arrival (flow), plus the
    // lane bow: streams arc apart perpendicular to the motion, peaking
    // mid-flight and exactly zero at both endpoints.
    vec2 fromPos = iFromRect.xy + cuv * iFromRect.zw;
    vec2 toPos = iToRect.xy + cuv * iToRect.zw;
    vec2 delta = (fromPos - toPos) * (1.0 - e);
    float mid = e * (1.0 - e) * 4.0;
    delta += perp * (lh - 0.5) * 2.0 * max(p_bow, 0.0) * mid;

    vTexCoord = cuv;
    vFlow = vec4(cuv, e, lh);
    gl_Position = modelViewProjectionMatrix * vec4(position + delta, 0.0, 1.0);
}
