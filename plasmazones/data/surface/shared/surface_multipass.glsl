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
// Bindings 2..9, the channel block of the shared binding table. The overlay
// family (multipass.glsl) declares the same eight; the pointer family
// (pointer_multipass.glsl) declares the first FOUR of the same block, because
// its contract keeps its own cap of two passes. Every declaration comes from
// the one table, so the daemon binds one layout for all of them.
layout(binding = 2) uniform sampler2D iChannel0;
layout(binding = 3) uniform sampler2D iChannel1;
layout(binding = 4) uniform sampler2D iChannel2;
layout(binding = 5) uniform sampler2D iChannel3;
layout(binding = 6) uniform sampler2D iChannel4;
layout(binding = 7) uniform sampler2D iChannel5;
layout(binding = 8) uniform sampler2D iChannel6;
layout(binding = 9) uniform sampler2D iChannel7;
#endif

// The blur chain's result, for a MAIN pass: the last UP pass, at the base level.
//
// IT LIVES HERE, NOT IN surface_blur.glsl, because this is where iChannel6 is
// declared and this is the header a main pass actually includes. surface_blur.glsl
// holds the chain's IMPLEMENTATION (the reach tables, the down and up combiners)
// and includes THIS file, not the other way round, so a main pass reaching for a
// helper defined over there would have to pull the whole pyramid in for one line.
// That is why all seven bundled packs sampled iChannel6 by hand, seventeen times
// between them, while the helper sat unused one header away.
//
// Which channel the chain lands on is a contract between the pass list and the
// main pass. Seventeen copies of the slot number is seventeen places to miss if
// it moves, none of which would fail to compile: they would read whatever else
// happened to be bound, or the 1x1 transparent fallback.
//
// TRANSPARENT IS A LEGITIMATE ANSWER HERE AND uHasBackdrop DOES NOT PREDICT
// IT. When a pack's buffer allocation fails, the compositor logs that the pack
// renders single-pass, clears the buffers, and the main pass then binds the
// 1x1 transparent fallback to every declared channel, deliberately, so that an
// unbound sampler2D cannot read the running composite. uHasBackdrop is
// unaffected and still reads 1.0, because the backdrop CAPTURE succeeded; only
// the chain that consumes it did not run. A pack that branches on
// uHasBackdrop >= 0.5 and then samples the chain with no second gate therefore
// draws a fully transparent pane instead of reaching its own no-backdrop
// fallback. Every bundled blur-family pack is written that way today. A pack
// that wants to be robust should treat a fully transparent chain result as the
// no-backdrop case too, rather than trusting the flag alone.
vec4 surfaceBlurTexel(vec2 uv) {
    return texture(iChannel6, uv);
}

#endif // PLASMAZONES_SURFACE_MULTIPASS_GLSL
