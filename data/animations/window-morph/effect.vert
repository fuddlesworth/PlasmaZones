// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Window-morph vertex shader — surface-extent pass-through. The geometry
// morph itself is done in the fragment stage (screen-space cross-fade
// between iFromRect and iToRect), so the vertex stage only needs to deliver
// the surface-spanning quad and a Y-down vTexCoord. `apply()` in the
// kwin-effect expands the drawn quad to the window's output for
// `fboExtent: "surface"`, so this quad covers the whole output and the
// fragment can paint the morphing rect anywhere between the old and new
// frames.
//
// Dual-branch like the rest of the tree since the UBO branches landed: the
// kwin branch places the output-spanning quad via the MVP matrix; the
// Qt-RHI branch (the settings preview host) emits the plain qt_Matrix quad
// and takes its uniforms from the AnimationUniforms block.

#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
#else
// Pulls in qt_Matrix (and the AnimationUniforms block) for the Qt-RHI
// branch; the kwin branch keeps this file's original minimal shape.
#include <animation_uniforms.glsl>
#endif

void main() {
#ifdef PLASMAZONES_KWIN
    // KWin's offscreen FBO is Y-up; flip so vTexCoord is the Y-down screen
    // UV the contract specifies, and place the output-spanning quad via the
    // MVP matrix.
    vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);
    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);
#else
    // The Qt-RHI quad's texCoord is Y-down already — no re-flip; the quad
    // spans the shader item, so qt_Matrix places it.
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
#endif
}
