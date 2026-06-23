// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Flow vertex shader — grid-deformed window-move ("liquid pour").
//
// Unlike window-morph (a single output-spanning quad that does the whole
// move in the fragment stage), flow runs on a tessellated grid that
// apply() builds over the window's DESTINATION frame rect (metadata
// `geometryGrid` controls the per-axis cell count). Anchoring the grid to
// the window keeps deformation resolution constant regardless of zone
// size, and lets this vertex stage displace each cell so the window
// streams into its zone instead of sliding rigidly: the edge facing the
// destination settles first, trailing rows lag by SPREAD and catch up.
//
// apply() emits texcoords as the card uv (v = 0 at the window's top), so
// the card uv IS the incoming texCoord — no screen-position
// reconstruction. The grid sits on the destination rect (iToRect), so a
// vertex's natural position already equals its settled position; the
// displacement is purely the pull back toward the old rect (iFromRect),
// which vanishes as the region arrives.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
// Geometry-morph endpoints (logical-screen px, x/y/w/h), pushed by the
// kwin-effect paint pipeline for any shader that declares them.
uniform vec4 iFromRect;
uniform vec4 iToRect;
// Per-vertex flow handed to the fragment: .xy = card uv, .z = arrival
// ease (0 = still at the old rect, 1 = settled at the destination).
layout(location = 1) out vec3 vFlow;
#endif

void main() {
#ifdef PLASMAZONES_KWIN
    // apply() emits the grid texcoords as card uv, but KWin's window-quad
    // texcoord convention is Y-flipped on upload (the same reason the
    // single-quad surface path probes handedness), so re-apply the
    // canonical `1.0 - texCoord.y` flip here — exactly as the shared kwin
    // vertex stage and window-morph's vert do. The result is card uv with
    // y = 0 at the window's top (Y-down), used for both the geometry
    // displacement and the content sampling so they stay aligned.
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    // Travel direction in screen space (y-down). A degenerate move (pure
    // resize) defaults to downward so it still reads as a gentle settle.
    vec2 fromC = iFromRect.xy + 0.5 * iFromRect.zw;
    vec2 toC = iToRect.xy + 0.5 * iToRect.zw;
    vec2 travel = toC - fromC;
    vec2 dir = (dot(travel, travel) > 1.0) ? normalize(travel) : vec2(0.0, 1.0);

    // Phase along the travel axis: leading edge (toward the destination)
    // is 1, trailing edge is 0.
    float phase = clamp(dot(cuv - 0.5, dir) + 0.5, 0.0, 1.0);

    // Staggered local progress: the leading edge starts at t = 0, the
    // trailing edge lags by SPREAD, and each ramps to 1 with a smoothstep
    // settle. legProgress() keeps the direction correct on reverse legs.
    const float SPREAD = 0.55;
    float tt = legProgress();
    float startT = (1.0 - phase) * SPREAD;
    float localT = clamp((tt - startT) / max(1.0 - SPREAD, 1.0e-3), 0.0, 1.0);
    float e = localT * localT * (3.0 - 2.0 * localT);

    // Each card point travels from its place in the old rect to its place
    // in the new rect. The grid already sits at the new rect, so the
    // displacement is the pull back toward the old rect, vanishing at
    // e = 1 (settled exactly where KWin placed the window).
    vec2 fromPos = iFromRect.xy + cuv * iFromRect.zw;
    vec2 toPos = iToRect.xy + cuv * iToRect.zw;
    vec2 delta = (fromPos - toPos) * (1.0 - e);

    // quad-space <-> screen-space is a pure translation at 1:1 logical
    // scale, so the screen-space delta applies directly to `position`.
    vec2 displaced = position + delta;

    vTexCoord = cuv;
    vFlow = vec3(cuv, e);
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
#else
    // Daemon RHI bake target: the geometry flow is compositor-only. Pass
    // the quad through so the shader still bakes (and is harmless if ever
    // run on the daemon path). Mirrors window-morph's daemon branch.
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
#endif
}
