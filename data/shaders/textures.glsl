// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// User-supplied image texture bindings (slots 7-10).
// Include from effect.frag with:
//   #include <textures.glsl>
//
// Requires common.glsl (for iTextureResolution UBO field).

#ifndef PLASMAZONES_TEXTURES_GLSL
#define PLASMAZONES_TEXTURES_GLSL

layout(binding = 7)  uniform sampler2D uTexture0;
layout(binding = 8)  uniform sampler2D uTexture1;
layout(binding = 9)  uniform sampler2D uTexture2;
layout(binding = 10) uniform sampler2D uTexture3;

// Compute UV from fragment coordinates for a given texture slot.
// Uses iTextureResolution[textureIndex] to normalize; falls back to 1.0 if unset.
vec2 textureUv(int textureIndex, vec2 fragCoord) {
    vec2 r = max(iTextureResolution[textureIndex], vec2(1.0));
    return fragCoord / r;
}

#endif // PLASMAZONES_TEXTURES_GLSL
