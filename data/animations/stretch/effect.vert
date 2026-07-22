// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stretch vertex shader — grid-deformed elastic ("rubber-band") window-move.
//
// The leading edge springs into the destination zone first while the body
// lags behind, so the window stretches taut along its travel axis, tapers
// perpendicular (like taffy), then snaps to fit with a single overshoot.
// Entirely in-plane — no faked depth — but it needs the grid because the
// stretch varies across the window (a 4-vertex quad could only scale
// uniformly). Runs on the same window-relative grid as the other geometry
// effects: apply() builds an NxN grid over the destination frame rect
// (metadata `geometryGrid`) and this stage displaces each vertex.
//
// Each vertex rides a back-ease (easeOutBack) that overshoots its target
// once and settles, staggered along the travel axis so leading vertices
// arrive ahead of trailing ones. A global perpendicular squash tent thins
// the window while it is in motion. Both vanish at t = 0 and t = 1, so the
// source and settled states are pristine.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

uniform mat4 modelViewProjectionMatrix;
uniform vec4 iFromRect;
uniform vec4 iToRect;
// Per-vertex data for the fragment: .xy = sampling card uv, .z = old->new
// cross-fade.
layout(location = 1) out vec3 vStretch;

// easeOutBack: rises to 1 with a single overshoot near the end, settling
// exactly at 1. OVERSHOOT scales the spring: 1.0 gives the classic ~10%
// overshoot, and 1.2 here for a slightly firmer snap.
float backOut(float x) {
    const float OVERSHOOT = 1.2;
    float c1 = 1.70158 * OVERSHOOT;
    float c3 = c1 + 1.0;
    float xm = x - 1.0;
    return 1.0 + c3 * xm * xm * xm + c1 * xm * xm;
}

void main() {
    const float PI = 3.14159265358979;
    const float SPREAD = 0.45;  // how far trailing verts lag the leading edge
    const float SQUASH = 0.16;  // perpendicular thinning at peak motion

    // Card uv with KWin's window-quad texcoord flip re-applied (see flow).
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    float tt = legProgress();

    // Travel/perp basis. A degenerate move (pure resize) stretches
    // vertically so it still reads.
    vec2 fromC = iFromRect.xy + 0.5 * iFromRect.zw;
    vec2 toC = iToRect.xy + 0.5 * iToRect.zw;
    vec2 travel = toC - fromC;
    vec2 dir = (dot(travel, travel) > 1.0) ? normalize(travel) : vec2(0.0, 1.0);
    vec2 perp = vec2(-dir.y, dir.x);

    // Per-vertex staggered spring: leading edge (s = 1) starts at t = 0,
    // trailing edge (s = 0) lags by SPREAD; each rides a back-ease that
    // overshoots once. The spread of progress across the window IS the
    // stretch.
    float s = clamp(dot(cuv - 0.5, dir) + 0.5, 0.0, 1.0);
    float denom = max(1.0 - SPREAD, 1.0e-3);
    float localLin = clamp((tt - (1.0 - s) * SPREAD) / denom, 0.0, 1.0);
    float e = backOut(localLin);

    // Window centre rides the same spring (sampled at the mid stagger) so
    // the perpendicular squash thins around a consistent axis.
    float eC = backOut(clamp((tt - 0.5 * SPREAD) / denom, 0.0, 1.0));
    vec2 centerPos = mix(fromC, toC, eC);

    // Per-vertex along-travel position (stretched), then thin perpendicular.
    vec2 fromPos = iFromRect.xy + cuv * iFromRect.zw;
    vec2 toPos = iToRect.xy + cuv * iToRect.zw;
    vec2 alongPos = mix(fromPos, toPos, e);

    vec2 rel = alongPos - centerPos;
    float alongComp = dot(rel, dir);
    // sin(PI * tt) goes negative past the ends, which would invert the squash into
    // a stretch on the tail of an overshoot. The envelope has no meaning outside the
    // leg; the intended overshoot rides on the rect, not on this.
    float perpComp = dot(rel, perp) * (1.0 - SQUASH * sin(PI * clamp(tt, 0.0, 1.0)));
    vec2 finalPos = centerPos + dir * alongComp + perp * perpComp;

    // quad-space <-> screen-space is a pure 1:1 translation, so the screen
    // delta from this vertex's settled position applies straight to it.
    vec2 displaced = position + (finalPos - toPos);

    vTexCoord = cuv;
    vStretch = vec3(cuv, clamp(eC, 0.0, 1.0));
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
}
