// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Default vertex stage for daemon anchor-extent animation shaders.
// SurfaceAnimator auto-assigns this to any animation effect that does
// not ship its own `effect.vert` (see surfaceanimator.cpp::runLeg).
// Surface-extent effects (bounce, fly-in, broken-glass, morph) supply
// their own pass-through `effect.vert` and never use this file; the
// kwin-effect path supplies `kKwinDefaultVertexSource` and likewise
// never uses this file — so this stage is daemon anchor-extent only.
//
// Card-space remap. The rasterised quad covers the captured FBO, which
// equals the shader anchor. When the anchor is larger than the visible
// card — a PopupFrame capture item that bundles the glow margin so the
// glow animates with the card through a transition — `iAnchorRectInTexture`
// is the card's UV sub-rect within that FBO. Dividing `texCoord` by it
// makes `vTexCoord` span [0, 1] over the CARD: every anchor-extent
// fragment shader works in card space, generative effects (fire,
// incinerate) stay confined to the card, and the glow margin lands
// outside [0, 1] where `boundaryMask` / edge fades crop it. The
// companion `surfaceColor()` (animation_uniforms.glsl) multiplies by
// the same rect to sample the card's region of `uTexture0`. A bare
// card-sized anchor carries the (0, 0, 1, 1) identity, so this is a
// pass-through there.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

void main() {
    // Guard the divisor: a degenerate (0, 0, 0, 0) rect would only
    // appear before SurfaceAnimator's first geometry sync, but the
    // extension already defaults to the (0, 0, 1, 1) identity — the
    // max() is belt-and-braces against a NaN reaching the rasteriser.
    vec2 rectSize = max(iAnchorRectInTexture.zw, vec2(1.0e-5));
    vTexCoord = (texCoord - iAnchorRectInTexture.xy) / rectSize;
    // Daemon animation shaders render DIRECT-TO-WINDOW (SurfaceAnimator's
    // shaderItem is not layer-enabled), so nothing absorbs the per-backend
    // NDC Y difference: OpenGL is Y-up-in-NDC, Vulkan is Y-down, and Qt-RHI
    // does not normalise the NDC Y of geometry the shader emits. qt_Matrix
    // carries that correction (identity on Vulkan, Y-flip on OpenGL) so the
    // quad presents upright on both. NOTE: overlay/zone shaders (zone.vert)
    // deliberately do NOT apply qt_Matrix — they render via layer.enabled and
    // Qt's composite already corrects them; flipping there would invert them.
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
