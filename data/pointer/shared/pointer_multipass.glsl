// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in MULTIPASS module: the iChannel buffer-pass sampler bindings for
// pointer packs that declare `"bufferShaders"` in metadata.json. `#include
// <pointer_multipass.glsl>` in a pack that samples a buffer-pass output.
// Single-pass packs never include it, so they declare no extra samplers.
//
// Each buffer pass renders into an FBO; its output is bound as iChannelN for
// downstream passes and the main effect. With `bufferFeedback` buffer pass N
// sees its OWN previous frame's output as iChannelN while it runs, which is
// how a pack keeps its own persistent state (a decaying trail canvas). The
// preview keeps a feedback pair only for a single buffer pass, so a
// feedback pack with two passes persists on the compositor alone; the
// validator lints that shape.
// iChannelResolution[N].xy (the pixel size of iChannelN) stays in the core
// contract (pointer_uniforms.glsl) because it is a pinned UBO member.

#ifndef PLASMAZONES_POINTER_MULTIPASS_GLSL
#define PLASMAZONES_POINTER_MULTIPASS_GLSL

#include <pointer_uniforms.glsl>

#ifdef PLASMAZONES_KWIN
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
#else
// Bindings 2-5, matching surface_multipass.glsl and the overlay category's
// shared/multipass.glsl so every family speaks the same iChannel dialect.
layout(binding = 2) uniform sampler2D iChannel0;
layout(binding = 3) uniform sampler2D iChannel1;
layout(binding = 4) uniform sampler2D iChannel2;
layout(binding = 5) uniform sampler2D iChannel3;
#endif

#endif // PLASMAZONES_POINTER_MULTIPASS_GLSL
