// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fold vertex shader — grid-deformed origami window-move.
//
// The window concertinas along its travel axis (creases run perpendicular
// to the direction of motion), folding up mid-flight and unfolding flat
// into the destination zone. Runs on the same window-relative grid as the
// flow effect: apply() builds an NxN grid over the padded composite canvas
// with destination-frame-relative texcoords
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

uniform mat4 modelViewProjectionMatrix;
uniform vec4 iFromRect;
uniform vec4 iToRect;
// Per-vertex data for the fragment: .xy = sampling card uv, .z = crease
// shade (1 = lit ridge, < 1 = shadowed valley, applied as a coverage
// fade — see effect.frag), .w = old->new cross-fade.
layout(location = 1) out vec4 vFold;

void main() {
    const float PI = 3.14159265358979;
    const float FOLDS = 5.0;      // crease count along the travel axis
    const float COMPRESS = 0.35;  // how far the accordion pulls in at full fold
    const float DEPTH = 0.13;     // perpendicular ridge amplitude (card uv)
    const float SHADE = 0.45;     // max valley darkening

    // Card uv with KWin's window-quad texcoord flip re-applied (see flow).
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    // Forward progress and a smoothstep ease for the translation.
    float tt = legProgress();
    // iTime is NOT bounded to [0,1] — an overshooting curve delivers its overshoot,
    // and legProgress() can hand this BELOW zero on a reverse leg (a t of 1.2 flips
    // to -0.2). Both terms below break on that, in opposite ways.
    //
    // The smoothstep polynomial is NON-MONOTONIC outside [0,1]: it turns around. At
    // tt = 1.15 it evaluates to 0.926, BELOW its value at tt = 1.0 — so the window
    // would reach its target and then snap BACKWARD ~7% at the very moment the
    // curve overshoots, bouncing the wrong way. Ease the clamped value and carry
    // the overshoot as a LINEAR extension past the ends, which keeps the mix
    // monotone while still letting the rect overshoot — the bounce is the point on
    // a geometry morph, so it is preserved rather than refused.
    float ec = clamp(tt, 0.0, 1.0);
    float ease = ec * ec * (3.0 - 2.0 * ec) + (tt - ec);
    // Tent fold amount: flat at both ends, fully folded at mid-flight. sin(PI*tt)
    // goes NEGATIVE past the ends (-0.588 at tt = 1.2), which would expand the card
    // instead of compressing it, invert the accordion ridges, and brighten the
    // shading instead of darkening it. The fold envelope has no meaning outside the
    // leg, so it is clamped.
    float f = sin(PI * ec);

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
    // POSITION takes the unbounded `ease` (the overshoot IS the bounce); SIZE takes a
    // bounded copy. Lerping the whole vec4 with `ease` extrapolates the
    // EXTENT: the engine bounds delivered progress to the overshoot envelope [-1, 2]
    // (legProgress flips a reverse leg's -1 to tt = +2), and even at that ceiling a
    // strong shrink computes rect.zw = 2*to - from: NEGATIVE width and height whenever
    // the window halves or more. `screenPos` then mirrors the card and inflates it: a
    // flipped, garbage-scaled window. This is verbatim the hazard window-morph
    // guards against, and it belongs here too.
    vec4 rect = vec4(mix(iFromRect.xy, iToRect.xy, ease), mix(iFromRect.zw, iToRect.zw, clamp(ease, 0.0, 1.0)));
    vec2 screenPos = rect.xy + duv * rect.zw;
    vec2 toPos = iToRect.xy + cuv * iToRect.zw;
    vec2 displaced = position + (screenPos - toPos);

    // Crease shade: ridges toward the viewer stay lit, valleys fade toward
    // the backdrop with fold depth (a coverage fade — see effect.frag).
    float shade = 1.0 - f * SHADE * (0.5 - saw);

    vTexCoord = cuv;
    vFold = vec4(cuv, shade, ease);
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
}
