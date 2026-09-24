// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in MULTIPASS module: the iChannel buffer-pass sampler bindings for
// pointer packs that declare `"bufferShaders"` in metadata.json. `#include
// <pointer_multipass.glsl>` in a pack that samples a buffer-pass output.
// Single-pass packs never include it, so they declare no extra samplers.
//
// OPTING IN TAKES TWO KEYS. `bufferShaders` alone does nothing: isMultipass
// comes from the `multipass` key, and a pack that declares the array without
// it has the whole array cleared at load and renders single-pass.
//
// Each buffer pass renders into an FBO; its output is bound as iChannelN for
// downstream passes and the main effect. With `bufferFeedback` buffer pass N
// sees its OWN previous frame's output as iChannelN while it runs, which is
// how a pack keeps its own persistent state (a decaying trail canvas). The
// preview keeps a feedback pair only for a single buffer pass, so a
// feedback pack with two passes persists on the compositor alone; the
// validator lints that shape.
//
// iChannelResolution[N].xy (the pixel size of iChannelN) stays in the core
// contract (pointer_uniforms.glsl) rather than here. On the PREVIEW branch it
// is a pinned UBO member and could not move without relaying the block; on the
// kwin branch it is a plain default-block uniform declared there so that a
// single-pass pack can still read it.

#ifndef PLASMAZONES_POINTER_MULTIPASS_GLSL
#define PLASMAZONES_POINTER_MULTIPASS_GLSL

#include <pointer_uniforms.glsl>

// FOUR channels, not the eight the other families declare. The pointer
// contract caps a chain at TWO passes, because a pointer chain runs on every
// output frame while the pointer is live, so iChannel2 and iChannel3 are
// headroom rather than reachable slots today.
#ifdef PLASMAZONES_KWIN
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
#else
// Bindings 2-5, the first four of the shared binding table's channel block
// (PhosphorShaders/ShaderBindings.h). surface_multipass.glsl and the overlay
// category's shared/multipass.glsl declare all eight of the same block, so
// every family speaks one iChannel dialect and this one simply stops earlier.
layout(binding = 2) uniform sampler2D iChannel0;
layout(binding = 3) uniform sampler2D iChannel1;
layout(binding = 4) uniform sampler2D iChannel2;
layout(binding = 5) uniform sampler2D iChannel3;
#endif

#endif // PLASMAZONES_POINTER_MULTIPASS_GLSL
