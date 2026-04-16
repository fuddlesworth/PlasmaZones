// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// User-supplied image texture bindings (slots 7-10).
// Include from effect.frag with:
//   #include <textures.glsl>
//
// Requires common.glsl (for iTextureResolution UBO field).

#ifndef PHOSPHORSHELL_TEXTURES_GLSL
#define PHOSPHORSHELL_TEXTURES_GLSL

layout(binding = 7)  uniform sampler2D uTexture0;
layout(binding = 8)  uniform sampler2D uTexture1;
layout(binding = 9)  uniform sampler2D uTexture2;
layout(binding = 10) uniform sampler2D uTexture3;

#endif // PHOSPHORSHELL_TEXTURES_GLSL
