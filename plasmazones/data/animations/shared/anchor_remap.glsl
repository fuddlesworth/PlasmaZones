// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Surface-UV to card-relative UV remap. Used by surface-extent shaders
// that need to convert vTexCoord into the captured card's [0,1] space.
//
// `anchorRemap` divides by `iResolution` and `iAnchorSize` and offsets
// by `iAnchorPosInFbo` — all three describe the space `vTexCoord` is
// delivered in:
//   * Surface-extent (both runtimes): vTexCoord spans the surface FBO.
//     iResolution = FBO size, iAnchorSize = card size, iAnchorPosInFbo
//     = the card's top-left within the FBO. anchorRemap folds surface-
//     UV into card UV.
//   * KWin anchor-extent: vTexCoord spans the EXPANDED rect (frame +
//     decoration shadow). iResolution = expanded size, iAnchorPosInFbo
//     = the shadow inset — the remap is load-bearing.
//   * Daemon anchor-extent: `animation.vert` already delivered card-
//     space vTexCoord, and iResolution / iAnchorSize both equal the
//     card with iAnchorPosInFbo = (0, 0) — so anchorRemap is identity.
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
