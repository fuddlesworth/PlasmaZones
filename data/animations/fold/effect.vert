// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fold vertex shader — grid-deformed origami window-move.
//
// The window concertinas along its travel axis (creases run perpendicular
// to the direction of motion), folding up mid-flight and unfolding flat
// into the destination zone. Runs on the same window-relative grid as the
// flow effect: apply() builds an NxN grid over the destination frame rect
// (metadata `geometryGrid`), and this stage displaces each vertex.
//
// Geometry is computed in card space (an orthonormal travel/perp basis),
// then mapped to screen pixels through the interpolated frame rect, so all
// per-axis scaling falls out of the rect mix. The displacement vanishes at
// t = 0 (window at the source rect, flat) and t = 1 (settled at the
// destination, flat); the accordion peaks at mid-flight.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
uniform vec4 iFromRect;
uniform vec4 iToRect;
// Per-vertex data for the fragment: .xy = sampling card uv, .z = crease
// shade (1 = lit ridge, < 1 = shadowed valley), .w = old->new cross-fade.
layout(location = 1) out vec4 vFold;
#endif

void main() {
#ifdef PLASMAZONES_KWIN
    const float PI = 3.14159265358979;
    const float FOLDS = 5.0;      // crease count along the travel axis
    const float COMPRESS = 0.35;  // how far the accordion pulls in at full fold
    const float DEPTH = 0.13;     // perpendicular ridge amplitude (card uv)
    const float SHADE = 0.45;     // max valley darkening

    // Card uv with KWin's window-quad texcoord flip re-applied (see flow).
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    // Forward progress and a smoothstep ease for the translation.
    float tt = legProgress();
    float ease = tt * tt * (3.0 - 2.0 * tt);
    // Tent fold amount: flat at both ends, fully folded at mid-flight.
    float f = sin(PI * tt);

    // Orthonormal travel/perp basis in (approx) card space. A degenerate
    // move (pure resize) folds along the vertical so it still reads.
    vec2 fromC = iFromRect.xy + 0.5 * iFromRect.zw;
    vec2 toC = iToRect.xy + 0.5 * iToRect.zw;
    vec2 travel = toC - fromC;
    vec2 dir = (dot(travel, travel) > 1.0) ? normalize(travel) : vec2(0.0, 1.0);
    vec2 perp = vec2(-dir.y, dir.x);

    // Decompose the card point onto the basis. At f = 0 this reconstructs
    // cuv exactly, so the unfolded states are pristine.
    float sRaw = dot(cuv - 0.5, dir); // along travel, centered
    float r = dot(cuv - 0.5, perp);   // perpendicular, centered

    // Accordion: compress along travel and add a triangle-wave ridge
    // perpendicular to it.
    float s01 = clamp(sRaw + 0.5, 0.0, 1.0);
    float phase = s01 * FOLDS;
    float saw = abs(fract(phase) - 0.5) * 2.0 - 0.5; // triangle in [-0.5, 0.5]
    float sDev = sRaw * (1.0 - f * COMPRESS);
    float ridge = saw * f * DEPTH;

    vec2 duv = vec2(0.5) + dir * sDev + perp * (r + ridge);

    // Map the deformed card point through the interpolated frame rect, and
    // express the result as a delta from this vertex's settled position
    // (the grid sits on iToRect). quad-space <-> screen-space is a pure
    // 1:1 translation, so the screen delta applies straight to `position`.
    vec4 rect = mix(iFromRect, iToRect, ease);
    vec2 screenPos = rect.xy + duv * rect.zw;
    vec2 toPos = iToRect.xy + cuv * iToRect.zw;
    vec2 displaced = position + (screenPos - toPos);

    // Crease shade: ridges toward the viewer stay lit, valleys darken with
    // fold depth.
    float shade = 1.0 - f * SHADE * (0.5 - saw);

    vTexCoord = cuv;
    vFold = vec4(cuv, shade, ease);
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
#else
    // Daemon RHI bake target: the fold is compositor-only. Pass the quad
    // through so the shader still bakes and is harmless if ever run.
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
#endif
}
