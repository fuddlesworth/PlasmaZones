// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared fullscreen-quad vertex stage for the SURFACE shader category. This is
// the LGPL shared contract for data/surface (same license as the sibling
// surface_uniforms.glsl) so it ships alongside the include and resolves from the
// SurfaceShaderItem / kwin-effect include paths with no per-pack vertex shader.
//
// DIRECT-TO-SCENE, like data/animations/shared/animation.vert (NOT layer-
// composited like zone.vert). The daemon hosts a surface pack with a
// SurfaceShaderItem that is a normal scene item (its SOURCE card is captured to
// an FBO; the shader item itself is not layer.enabled), exactly as
// SurfaceAnimator renders its transition shaders. Qt-RHI does not normalise the
// NDC Y of geometry the shader emits, so this stage MUST multiply by qt_Matrix:
// it is identity on Y-down backends (Vulkan) and a Y-flip on Y-up-in-NDC
// backends (OpenGL), keeping the quad upright on both. (qt_Matrix lives in the
// daemon UBO branch of surface_uniforms.glsl; SurfaceUniformProfile fills it.)
//
// vTexCoord is the Y-down screen UV the fragment contract expects: the daemon's
// Qt-RHI uTexture0 is top-origin, so surfaceTexel() samples it directly with no
// flip (mirrors animation_uniforms.glsl::surfaceColor's daemon branch).
//
// Unlike zone.vert this does NOT #include <common.glsl> and emits NO vFragCoord:
// the surface fragment contract reads pixel coordinates via surfacePixel(uv)
// (driven by uSurfaceSize), not a vertex-supplied vFragCoord varying.

#version 450

#include <surface_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
