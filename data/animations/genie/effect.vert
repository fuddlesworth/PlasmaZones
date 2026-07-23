// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Genie vertex shader — grid-deformed minimize-to-icon ("magic lamp").
//
// Runs on the same tessellated window grid as flow (metadata
// `geometryGrid`; apply() builds it over the window's frame rect — or its
// padded decoration canvas, with frame-relative texcoords — since a
// minimize leg records no morph destination). Each grid vertex travels
// from its place in the window toward its place in the task-manager icon
// rect (iIconRect), staggered so the edge facing the icon leads and the
// far edge lags — the stagger is what draws the lamp funnel, exactly the
// same insight flow uses for its pour.
//
// Direction handling: the host flips iTime on the reverse
// (going-to-minimized) leg, so `1 - iTime` is the swallow progress on
// BOTH legs — it runs 0 → 1 while minimizing and 1 → 0 while restoring.
// No iIsReversed branch is needed.
//
// Coordinate space: iSurfaceScreenPos.xy + iAnchorSize give the window's
// frame rect in the same global logical pixels as iIconRect, and
// quad-space <-> screen-space is a pure translation at 1:1 logical scale,
// so a screen-space displacement applies directly to `position` (same
// rationale as flow).

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
// Task-manager icon rect (logical screen px, x/y/w/h), pushed by the
// kwin-effect paint pipeline for any shader that declares it.
// (0, 0, 0, 0) means the window sits in no task manager.
uniform vec4 iIconRect;
// Per-vertex genie state handed to the fragment: .xy = card uv, .z =
// swallow progress (0 = window at rest, 1 = fully inside the icon).
layout(location = 1) out vec3 vGenie;
#endif

void main() {
#ifdef PLASMAZONES_KWIN
    // apply() emits the grid texcoords as card uv, Y-flipped on upload by
    // KWin's window-quad convention — re-apply the canonical flip, same
    // as flow and the shared kwin vertex stage.
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    // Window frame rect in global logical px — the same space iIconRect
    // is pushed in.
    vec4 W = vec4(iSurfaceScreenPos.xy, iAnchorSize);

    // Icon target. A window with no task-manager entry collapses into a
    // sliver on its own bottom edge instead, so the animation still
    // reads as the window going away downward.
    vec4 I = iIconRect;
    if (I.z < 1.0 || I.w < 1.0) {
        I = vec4(W.x + 0.5 * W.z - 24.0, W.y + W.w - 6.0, 48.0, 6.0);
    }

    // Swallow progress, both legs (see header comment).
    float p = 1.0 - clamp(iTime, 0.0, 1.0);

    // Stagger by proximity to the icon along the dominant travel axis:
    // the icon-facing edge starts moving at p = 0, the far edge lags by
    // SPREAD and catches up by p = 1.
    vec2 wc = W.xy + 0.5 * W.zw;
    vec2 ic = I.xy + 0.5 * I.zw;
    vec2 d = ic - wc;
    float lead;
    if (abs(d.y) >= abs(d.x)) {
        lead = (d.y >= 0.0) ? cuv.y : 1.0 - cuv.y;
    } else {
        lead = (d.x >= 0.0) ? cuv.x : 1.0 - cuv.x;
    }
    const float SPREAD = 0.45;
    float pv = clamp((p - (1.0 - lead) * SPREAD) / max(1.0 - SPREAD, 1.0e-3), 0.0, 1.0);
    pv = pv * pv * (3.0 - 2.0 * pv);

    // Each card point travels from its place in the window to its place
    // in the icon. Leading vertices converge first, which draws the
    // funnel; at p = 1 every vertex sits inside the icon rect.
    vec2 natural = W.xy + cuv * W.zw;
    vec2 target = I.xy + cuv * I.zw;
    vec2 displaced = position + (target - natural) * pv;

    vTexCoord = cuv;
    vGenie = vec3(cuv, p);
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
#else
    // Daemon RHI bake target: the genie deformation is compositor-only
    // (no window grid or icon target exists on an overlay surface). Pass
    // the quad through so the shader bakes; the fragment stage degrades
    // to a plain fade there. Mirrors flow's daemon branch.
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
#endif
}
