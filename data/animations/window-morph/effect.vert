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
// frames.
//
// COMPOSITOR-ONLY by design: unlike the surface-extent verts in morph and
// bounce, this file has no `#ifdef PLASMAZONES_KWIN` split and leaves
// `modelViewProjectionMatrix` unguarded, which the strict SPIR-V bake
// rejects. That is safe only because this pack's appliesTo is ["geometry"]
// — `shaderEffectIsCompositorOnly()` is true, so the daemon never bakes it.
// Do NOT copy this shape into a pack that declares "appearance"; take the
// dual-branch form from morph/bounce instead.

#version 450

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
