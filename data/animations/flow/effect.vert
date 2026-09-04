// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Flow vertex shader — grid-deformed window-move ("liquid pour").
//
// Unlike window-morph (a single output-spanning quad that does the whole
// move in the fragment stage), flow runs on a tessellated grid that
// apply() builds over the window's padded composite canvas, with texcoords
// kept DESTINATION-frame-relative (metadata
// `geometryGrid` controls the per-axis cell count). Anchoring the grid to
// the window keeps deformation resolution constant regardless of zone
// size, and lets this vertex stage displace each cell so the window
// streams into its zone instead of sliding rigidly: the edge facing the
// destination settles first, trailing rows lag by `spread` and catch up.
//
// apply() emits texcoords relative to the destination FRAME (v = 0 at the
// window's top before KWin's upload flip — see the re-flip note in main()).
// Because the texcoords are destination-frame-relative, a vertex's natural
// position already equals its settled position, so the displacement below is
// purely the pull back toward the old rect (iFromRect), which vanishes as
// the region arrives.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
// Geometry-morph endpoints (logical-screen px, x/y/w/h), pushed by the
// kwin-effect paint pipeline for any shader that declares them. On the
// UBO branch both rects come from the AnimationUniforms transition tail.
uniform vec4 iFromRect;
uniform vec4 iToRect;
#endif
// Per-vertex flow handed to the fragment: .xy = card uv, .z = arrival
// ease (0 = still at the old rect, 1 = settled at the destination).
layout(location = 1) out vec3 vFlow;

void main() {
    // apply() emits the grid texcoords as card uv, but KWin's window-quad
    // texcoord convention is Y-flipped on upload (the same reason the
    // single-quad surface path probes handedness), so re-apply the
    // canonical `1.0 - texCoord.y` flip here — exactly as the shared kwin
    // vertex stage and window-morph's vert do. The result is card uv with
    // y = 0 at the window's top (Y-down), used for both the geometry
    // displacement and the content sampling so they stay aligned.
#ifdef PLASMAZONES_KWIN
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);
#else
    // The Qt-RHI quad's texCoord is Y-down already — no re-flip.
    // ...and spans the captured canvas: fold the anchor sub-rect so cuv is
    // card space (identity rect on a bare-card capture = passthrough).
    vec2 cuv = (texCoord - iAnchorRectInTexture.xy) / max(iAnchorRectInTexture.zw, vec2(1.0e-5));
#endif

    // Travel direction in screen space (y-down). A pure resize has no rigid
    // translation, so the direction comes from the growth axis instead; only
    // a leg that neither moved nor resized falls through to downward.
    // From the leg's RIGID translation, not its centre delta: a window
    // pinned at one edge and grown moves its centre by half the size change
    // while standing still, and reading that as travel staggers a stretch.
    vec2 dir = legDirection(iFromRect, iToRect);

    // Phase along the travel axis: leading edge (toward the destination)
    // is 1, trailing edge is 0.
    float phase = clamp(dot(cuv - 0.5, dir) + 0.5, 0.0, 1.0);

    // Staggered local progress: the leading edge starts at t = 0, the
    // trailing edge lags by `spread`, and each ramps to 1 with a smoothstep
    // settle. legProgress() keeps the direction correct on reverse legs.
    // Scaled by how much of the leg is travel: the trailing lag is what
    // makes a moving window flow, but on an anchored resize it only makes
    // parts of one window reach their final extent at different times.
    // A pure stretch therefore settles uniformly; a real slide is unchanged.
    // p_spread was the baked SPREAD = 0.55 constant before it was declared;
    // the clamp mirrors phosphor-stream's p_spread bound so a hand-edited
    // value cannot push startT past 1.
    float spread = clamp(p_spread, 0.0, 0.8) * legTravelShare(iFromRect, iToRect);
    float tt = legProgress();
    float startT = (1.0 - phase) * spread;
    // The max() is defensive rather than reachable: spread is at most 0.8,
    // so the divisor never drops under 0.2. It guards the day the clamp
    // is raised toward 1, and stretch carries the identical guard.
    float localT = clamp((tt - startT) / max(1.0 - spread, 1.0e-3), 0.0, 1.0);
    float e = localT * localT * (3.0 - 2.0 * localT);

    // Each card point travels from its place in the old rect to its place
    // in the new rect. The grid already sits at the new rect, so the
    // displacement is the pull back toward the old rect, vanishing at
    // e = 1 (settled exactly where KWin placed the window).
    vec2 fromPos = iFromRect.xy + cuv * iFromRect.zw;
    vec2 toPos = iToRect.xy + cuv * iToRect.zw;
    vec2 delta = (fromPos - toPos) * (1.0 - e);

    vTexCoord = cuv;
    vFlow = vec3(cuv, e);
#ifdef PLASMAZONES_KWIN
    // quad-space <-> screen-space is a pure translation at 1:1 logical
    // scale, so the screen-space delta applies directly to `position`.
    vec2 displaced = position + delta;
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
#else
    // Qt-RHI path: `position` is clip-space over the shader item, whose
    // extent is iResolution logical px, so the logical-px delta converts at
    // 2 / iResolution. The clip-space quad's +y matches vTexCoord's Y-down
    // (qt_Matrix carries the per-backend NDC correction), so the sign
    // carries straight through. The item runs the pack's geometryGrid mesh
    // (ShaderNodeRhi::setGridSubdivisions), so the per-vertex stagger warps
    // here exactly as the kwin window-quad grid does.
    vec2 displaced = position + delta * 2.0 / max(iResolution, vec2(1.0));
    gl_Position = qt_Matrix * vec4(displaced, 0.0, 1.0);
#endif
}
