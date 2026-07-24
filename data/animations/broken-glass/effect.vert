// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Broken-glass vertex shader — pass-through. The rasterised quad covers
// the entire surface FBO (sized to the QQuickWindow's contentItem by
// the daemon's surface-extent geometry, or the captured window frame
// on the kwin-effect path). The fragment shader uses
// `iAnchorPosInFbo / iAnchorSize / iResolution` to convert each
// fragment's surface-UV into anchor-relative coords; the shard
// cascade naturally fills the entire shader-item region so shards can
// fly into the part of the surface that sits outside the original
// anchor.
//
// vTexCoord is a Y-down screen UV on both paths. The daemon's Qt-RHI
// quad delivers texCoord Y-down already; KWin's `OffscreenData::paint`
// delivers it from a Y-up offscreen FBO, so the kwin path flips it.
// `gl_Position` also differs (KWin needs the MVP matrix). See
// `animation_uniforms.glsl` for the contract. Matches morph/effect.vert.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
#endif

void main() {
#ifdef PLASMAZONES_KWIN
    // kwin's offscreen FBO is Y-up; flip so vTexCoord is the Y-down
    // screen UV the shader contract specifies on both runtimes.
    vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);
    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);
#else
    vTexCoord = texCoord;
    // Direct-to-window daemon path: qt_Matrix carries the per-backend NDC
    // Y-flip (identity on Vulkan, Y-flip on OpenGL). See animation.vert for
    // the full rationale and why overlay (zone.vert) shaders omit it.
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
#endif
}
