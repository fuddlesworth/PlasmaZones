// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Window-morph vertex shader — surface-extent pass-through. The geometry
// morph itself is done in the fragment stage (screen-space cross-fade
// between iFromRect and iToRect), so the vertex stage only needs to deliver
// the surface-spanning quad and a Y-down vTexCoord. `apply()` in the
// kwin-effect expands the drawn quad to the window's output for
// `fboExtent: "surface"`, so this quad covers the whole output and the
// fragment can paint the morphing rect anywhere between the old and new
// frames. Mirrors the other surface-extent vertex shaders (morph, bounce).

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

uniform mat4 modelViewProjectionMatrix;

void main() {
    // KWin's offscreen FBO is Y-up; flip so vTexCoord is the Y-down screen
    // UV the contract specifies, and place the output-spanning quad via the
    // MVP matrix.
    vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);
    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);
}
