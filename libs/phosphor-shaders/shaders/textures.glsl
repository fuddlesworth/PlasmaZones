// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// User-supplied image texture bindings (slots 11-14).
// Include from effect.frag with:
//   #include <textures.glsl>
//
// Requires common.glsl (for iTextureResolution UBO field).

#ifndef PHOSPHORSHADERS_TEXTURES_GLSL
#define PHOSPHORSHADERS_TEXTURES_GLSL

layout(binding = 11) uniform sampler2D uTexture0;
layout(binding = 12) uniform sampler2D uTexture1;
layout(binding = 13) uniform sampler2D uTexture2;
layout(binding = 14) uniform sampler2D uTexture3;

#endif // PHOSPHORSHADERS_TEXTURES_GLSL
