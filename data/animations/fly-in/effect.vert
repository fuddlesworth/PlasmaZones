// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fly-in vertex shader — pass-through. The rasterised quad covers the
// entire surface FBO (sized to the QQuickWindow's contentItem by the
// daemon's surface-extent geometry, or to the window's output by the
// kwin-effect's apply() override). The companion `effect.frag` uses
// `anchorRemap` to convert each fragment's surface-UV into the window's
// own [0,1] space, then rigidly translates the window horizontally for
// the slide.
//
// Pass-through here means: no clip-space remap. Qt's QSG geometry for a
// surface-sized QQuickShaderEffect is already a (-1..1) quad covering
// the FBO, and `texCoord` interpolates 0..1 over that quad — exactly
// the contract the fragment shader's `anchorRemap` expects. gl_Position
// differs per runtime: KWin needs the modelViewProjectionMatrix to
// place the redirected quad, the daemon emits clip space directly.
//
// This replaces the earlier vertex-driven remap, which translated the
// quad geometry itself. That approach assumed `position` was window-
// sized, but the kwin-effect's apply() override expands the quad to the
// whole output — so the window texture stretched to fill the output
// ("balloons to full screen"). The surface-extent `anchorRemap`
// contract (see anchor_remap.glsl) is the model broken-glass / morph /
// bounce already use; fly-in now matches it.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
#endif

void main() {
    // texCoord -> Y-down screen UV. The daemon's Qt-RHI quad delivers
    // it Y-down already; KWin's offscreen FBO is Y-up, so flip on that
    // path. Mirrors `kKwinDefaultVertexSource` and the canonical
    // header's vertex-stage contract.
#ifdef PLASMAZONES_KWIN
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
