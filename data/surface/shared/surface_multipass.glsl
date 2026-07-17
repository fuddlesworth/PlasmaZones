// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in MULTIPASS module: the iChannel buffer-pass sampler bindings for surface
// packs that declare `"bufferShaders"` in metadata.json. `#include
// <surface_multipass.glsl>` in a pack (or a shared helper like surface_blur.glsl)
// that samples a buffer-pass output. Single-pass packs (the border) never
// include it, so they declare no extra samplers.
//
// Each buffer pass renders into an FBO; its output is bound as iChannelN for
// downstream passes and the main effect, the same iChannel dialect the
// overlay/animation categories use. iChannelResolution[N].xy (the pixel size of
// iChannelN) stays in the core contract (surface_uniforms.glsl) because it is a
// pinned UBO member on the daemon; only the samplers live here.

#ifndef PLASMAZONES_SURFACE_MULTIPASS_GLSL
#define PLASMAZONES_SURFACE_MULTIPASS_GLSL

#include <surface_uniforms.glsl>

#ifdef PLASMAZONES_KWIN
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
#else
// Bindings 2-5, matching the overlay category's shared/multipass.glsl so surface
// and overlay packs speak the same iChannel binding dialect.
layout(binding = 2) uniform sampler2D iChannel0;
layout(binding = 3) uniform sampler2D iChannel1;
layout(binding = 4) uniform sampler2D iChannel2;
layout(binding = 5) uniform sampler2D iChannel3;
#endif

#endif // PLASMAZONES_SURFACE_MULTIPASS_GLSL
