// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in MULTIPASS module: the iChannel buffer-pass sampler bindings for surface
// packs that run buffer passes. `#include <surface_multipass.glsl>` in a pack (or
// a shared helper like surface_blur.glsl) that samples a buffer-pass output.
// Single-pass packs (the border) never include it, so they declare no extra
// samplers.
//
// THE OPT-IN IS `"multipass": true`, NOT `"bufferShaders"`. The two are separate
// metadata keys and only the first one gates anything: isMultipass is read from
// `multipass` alone, and the registry's single-pass coherence block CLEARS
// bufferShaderPaths, bufferWraps, bufferFilters, bufferFeedback, depthBuffer and
// bufferScale whenever isMultipass is false. A pack that lists its buffer passes
// and omits the flag therefore loads with every one of them discarded, renders
// single-pass, and gets no diagnostic for it. Declare both.
//
// Each buffer pass renders into an FBO; its output is bound as iChannelN for
// downstream passes and the main effect, the same iChannel dialect the
// overlay/animation categories use. Eight channels, one per pass of the
// kMaxBufferPasses budget (PhosphorShaders/ShaderBindings.h is the table).
// iChannelResolution[N].xy (the pixel size of iChannelN) stays in the core
// contract (surface_uniforms.glsl) because it is a pinned UBO member on the
// daemon, and it covers the FIRST FOUR channels only; size iChannel4..7 with
// textureSize(iChannelN, 0), which the builtin Kawase passes do for every
// channel.

#ifndef PLASMAZONES_SURFACE_MULTIPASS_GLSL
#define PLASMAZONES_SURFACE_MULTIPASS_GLSL

#include <surface_uniforms.glsl>

#ifdef PLASMAZONES_KWIN
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
uniform sampler2D iChannel4;
uniform sampler2D iChannel5;
uniform sampler2D iChannel6;
uniform sampler2D iChannel7;
#else
// Bindings 2..9, the channel block of the shared binding table every RHI
// family declares (overlay multipass.glsl, pointer_multipass.glsl), so the
// daemon binds one layout for all of them.
layout(binding = 2) uniform sampler2D iChannel0;
layout(binding = 3) uniform sampler2D iChannel1;
layout(binding = 4) uniform sampler2D iChannel2;
layout(binding = 5) uniform sampler2D iChannel3;
layout(binding = 6) uniform sampler2D iChannel4;
layout(binding = 7) uniform sampler2D iChannel5;
layout(binding = 8) uniform sampler2D iChannel6;
layout(binding = 9) uniform sampler2D iChannel7;
#endif

#endif // PLASMAZONES_SURFACE_MULTIPASS_GLSL
