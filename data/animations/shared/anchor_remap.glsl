// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Surface-UV to anchor-relative UV remap. Used by surface-extent
// shaders (broken-glass, morph) that need to convert vTexCoord (which
// spans the full surface FBO) into the captured anchor's UV space.
//
// For anchor-extent shaders `iAnchorPosInFbo` is `(0, 0)` and
// `iAnchorSize == iResolution`, so the remap collapses to identity.
// Surface-extent shaders (`fboExtent: "surface"`) get a non-zero
// `iAnchorPosInFbo` and output-sized `iResolution` on both the daemon
// and the kwin-effect runtimes, so the remap is load-bearing there.
//
// Requires: <animation_uniforms.glsl> (provides iResolution,
// iAnchorSize, iAnchorPosInFbo).

#ifndef ANCHOR_REMAP_GLSL
#define ANCHOR_REMAP_GLSL

vec2 anchorRemap(vec2 uv) {
    vec2 resSafe = max(iResolution, vec2(1.0));
    vec2 anchorSizePx = max(iAnchorSize, vec2(1.0));
    vec2 anchorTopLeftUv = iAnchorPosInFbo / resSafe;
    vec2 anchorSizeUv = anchorSizePx / resSafe;
    return (uv - anchorTopLeftUv) / anchorSizeUv;
}

#endif
