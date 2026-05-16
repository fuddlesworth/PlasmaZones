// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Morph vertex shader — pass-through. The rasterised quad covers the
// entire surface FBO (sized to the QQuickWindow's contentItem by the
// daemon's surface-extent geometry). The fragment shader uses
// `iAnchorPosInFbo / iAnchorSize / iResolution` to convert each
// fragment's surface-UV into anchor-relative coords, clip to the
// anchor's region, and apply the warp.
//
// Pass-through here means: no clip-space remap. Qt's QSG geometry for
// a surface-sized QQuickShaderEffect is already a (-1..1) quad
// covering the FBO, and `texCoord` (and the kwin-effect's `texCoord`
// attribute) interpolates 0..1 over that quad — exactly the contract
// the fragment shader expects.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
#endif

void main() {
    // texCoord pass-through — KWin and the daemon both deliver it
    // Y=0-at-top. Mirrors `kKwinDefaultVertexSource` /
    // `kDefaultVertexShaderSource` on the C++ side. Only gl_Position
    // differs: KWin needs the MVP matrix, the daemon emits clip space.
    vTexCoord = texCoord;
#ifdef PLASMAZONES_KWIN
    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);
#else
    gl_Position = vec4(position, 0.0, 1.0);
#endif
}
