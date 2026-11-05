// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// User-supplied image texture bindings (slots 11-14).
// Include from effect.frag with:
//   #include <textures.glsl>
//
// Requires common.glsl (for the iTextureResolution UBO field).
//
// HOW A TEXTURE GETS INTO A SLOT: declare an `image`-typed parameter in the
// pack's metadata.json. The registry resolves its path (relative to the pack
// dir, traversal-checked) and binds it to the slot its declaration order
// earns, so uTexture0 is the FIRST image parameter and so on. A slot no
// parameter claims is bound to a 1x1 transparent dummy rather than left
// unbound, so sampling it is defined and reads transparent black.
// iTextureResolution[N].xy carries the bound image's pixel size, or (1,1) for
// an unclaimed slot.

#ifndef PLASMAZONES_TEXTURES_GLSL
#define PLASMAZONES_TEXTURES_GLSL

layout(binding = 11) uniform sampler2D uTexture0;
layout(binding = 12) uniform sampler2D uTexture1;
layout(binding = 13) uniform sampler2D uTexture2;
layout(binding = 14) uniform sampler2D uTexture3;

#endif // PLASMAZONES_TEXTURES_GLSL
