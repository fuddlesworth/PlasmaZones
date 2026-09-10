// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared fullscreen-quad vertex stage for the POINTER shader family, modelled
// on data/surface/shared/surface.vert. It ships alongside the include and
// resolves from the preview / kwin-effect include paths, so a pack needs no
// vertex shader of its own.
//
// DIRECT-TO-SCENE, like surface.vert. Qt-RHI does not normalise the NDC Y of
// geometry the shader emits, so this stage MUST multiply by qt_Matrix: it is
// identity on Y-down backends (Vulkan) and a Y-flip on Y-up-in-NDC backends
// (OpenGL), keeping the quad upright on both. (qt_Matrix lives in the preview
// UBO branch of pointer_uniforms.glsl.) The compositor supplies its own
// vertex stage through GLShader traits and never compiles this file.
//
// vTexCoord is the screen UV the fragment contract expects; pointerPixel(uv)
// turns it into top-down canvas px on either runtime.
//
// PREVIEW ONLY. Do NOT name this file as a pack's `vertexShader`. qt_Matrix
// exists only in the preview's UBO branch, so on the compositor this stage
// fails to compile and the pack falls back to the built-in vertex source with
// a warning nobody reads; the preview and the validator's preview bake would
// both pass, hiding that the declared stage is dead where it ships. The
// validator rejects a pack whose vertexShader resolves here for that reason.
// A pack that needs its own vertex stage writes one against the KWin branch.

#version 450

#include <pointer_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
