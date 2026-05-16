// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
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
// texCoord is passed through unchanged on both paths — KWin's
// `OffscreenData::paint` and the daemon's Qt-RHI quad deliver it in
// the same Y=0-at-top orientation. Only `gl_Position` differs (KWin
// needs the MVP matrix). See `animation_uniforms.glsl` for the
// contract. Matches morph/effect.vert.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
#endif

void main() {
    vTexCoord = texCoord;
#ifdef PLASMAZONES_KWIN
    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);
#else
    gl_Position = vec4(position, 0.0, 1.0);
#endif
}
