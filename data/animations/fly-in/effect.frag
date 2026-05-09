// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fly-in fragment shader — sample the redirected surface texture as-is.
// The visible motion is entirely produced by the companion `effect.vert`
// translating the quad horizontally; this stage just renders the texture
// at full opacity. Kept deliberately minimal so the vert path is the
// thing under test.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(uTexture0, vTexCoord);
}
