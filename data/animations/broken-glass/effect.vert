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
// Y-flip on the kwin path is mandatory — see
// `data/animations/shared/animation_uniforms.glsl` for the full
// contract. KWin's `OffscreenData::paint` populates `texCoord` with
// Y-up FBO sampling coordinates; the canonical daemon Y=0-at-top
// convention requires this flip. Matches morph/effect.vert.

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
    vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);
    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);
#else
    vTexCoord = texCoord;
    gl_Position = vec4(position, 0.0, 1.0);
#endif
}
