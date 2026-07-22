// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ripple-snap vertex shader — grid-deformed impact window-move.
//
// The window accelerates rigidly into its destination zone (a "slam"),
// then on arrival a decaying wave travels across the grid from the leading
// edge — the edge that hit the zone boundary — like a sheet snapping taut.
// Runs on the same window-relative grid as flow/fold: apply() builds an
// NxN grid over the destination frame rect (metadata `geometryGrid`) and
// this stage displaces each vertex.
//
// The wave is a transverse (in-plane, perpendicular-to-travel) ripple so
// it is visible head-on under KWin's orthographic projection, plus a
// crest-shade factor for depth cueing. It is zero during travel, peaks at
// impact, and decays to flat at the destination.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

uniform mat4 modelViewProjectionMatrix;
uniform vec4 iFromRect;
uniform vec4 iToRect;
// Per-vertex data for the fragment: .xy = sampling card uv, .z = ripple
// shade, .w = old->new cross-fade.
layout(location = 1) out vec4 vRip;

void main() {
    const float TWO_PI = 6.28318530718;
    const float IMPACT = 0.4;          // fraction of the leg spent travelling
    const float SPEED = 1.6;           // wavefront speed across the window
    const float FREQ = 3.0;            // oscillations in the wave packet
    const float SPATIAL_DECAY = 3.5;   // falloff behind the wavefront
    const float AMP = 0.06;            // transverse amplitude (card uv)
    const float SHADE = 0.5;           // crest/trough shade gain

    // Card uv with KWin's window-quad texcoord flip re-applied (see flow).
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    float tt = legProgress();

    // Travel/perp basis. A degenerate move (pure resize) ripples from the
    // bottom edge so it still reads.
    vec2 fromC = iFromRect.xy + 0.5 * iFromRect.zw;
    vec2 toC = iToRect.xy + 0.5 * iToRect.zw;
    vec2 travel = toC - fromC;
    vec2 dir = (dot(travel, travel) > 1.0) ? normalize(travel) : vec2(0.0, 1.0);
    vec2 perp = vec2(-dir.y, dir.x);

    // Rigid travel: accelerate into the zone so the arrival reads as a
    // slam, then sit at the destination for the ripple phase.
    float te = clamp(tt / IMPACT, 0.0, 1.0);
    float teE = te * te;
    vec4 rect = mix(iFromRect, iToRect, teE);

    // Ripple phase: a wavefront leaves the leading edge (proj = 1) and
    // crosses to the trailing edge, decaying behind the front and over time.
    float rp = clamp((tt - IMPACT) / max(1.0 - IMPACT, 1.0e-3), 0.0, 1.0);
    float proj = dot(cuv - 0.5, dir) + 0.5; // 0..1 along travel
    float d = 1.0 - clamp(proj, 0.0, 1.0);  // distance from the contact edge
    float x = rp * SPEED - d;               // >0 once the front has passed
    float osc = sin(x * FREQ * TWO_PI);
    float spatialEnv = smoothstep(0.0, 0.08, x) * exp(-max(x, 0.0) * SPATIAL_DECAY);
    float timeEnv = 1.0 - rp;               // settle flat by the end
    float wave = osc * spatialEnv * timeEnv;

    // Transverse in-plane displacement + map through the interpolated rect.
    vec2 duv = cuv + perp * (wave * AMP);
    vec2 screenPos = rect.xy + duv * rect.zw;
    vec2 toPos = iToRect.xy + cuv * iToRect.zw;
    vec2 displaced = position + (screenPos - toPos);

    // Fade-only: brightening a premultiplied sample (scaling RGB and
    // alpha together) would push alpha past 1.0 in the FBO, so cap at
    // 1.0; the sub-1 side is the coverage fade the frag applies to the
    // troughs (see effect.frag).
    float shade = clamp(1.0 + wave * SHADE, 0.6, 1.0);

    vTexCoord = cuv;
    vRip = vec4(cuv, shade, teE);
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
}
