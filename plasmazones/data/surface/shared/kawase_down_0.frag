// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Standard buffer pass: dual Kawase DOWN level 0, the backdrop capture into
// the quarter-res base.
//
// OPTING IN TAKES THREE KEYS, not just the tokens, and a pack that declares
// only the tokens renders a fully transparent pane with no warning at any
// level. In metadata.json:
//
//   "multipass": true        — isMultipass comes from THIS key alone. Without
//                              it the registry clears bufferShaderPaths, the
//                              whole buffer loop is skipped, and the chain
//                              never runs.
//   "bufferShaders": [ ... ] — the seven builtin:kawase-* tokens, in order.
//                              They are positional, so the order is the chain.
//   "needsBackdrop": true    — this pass's only source is backdropTexel(),
//                              which reads nothing without it.
//
// See surface_blur.glsl for the chain and its bufferScales.

#version 450
#include <surface_blur.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = surfaceKawaseDownBackdrop(vTexCoord, surfaceKawaseOffset(surfaceKawaseDepth()));
}
