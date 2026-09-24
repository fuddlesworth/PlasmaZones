// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in audio-spectrum helpers for ANIMATION shader packs — the transition
// cousin of surface_audio.glsl (surface family) and data/overlays/shared/
// audio.glsl (overlay family). `#include <audio.glsl>` AFTER
// animation_uniforms.glsl in a pack that reacts to the CAVA spectrum, declare
// `"audio": true` in the pack's metadata.json, and guard on the helpers (they
// return 0 when audio is off).
//
// Unlike the overlay family, where audio is implicit/session-global, animation
// packs must declare the metadata flag: the kwin-effect keys its CAVA run-gate
// on it, keeping the provider warm while an audio pack is assigned anywhere in
// the shader profile tree so a transition's FIRST frame already has a spectrum
// (cava spawn latency would otherwise eat a whole open/close leg).
//
// THE DAEMON READS THAT FLAG NOWHERE, and that cuts both ways. The feed is
// unconditional, so nothing needs the flag to receive a spectrum:
// SurfaceAnimator::setAudioSpectrum pushes to every attached animation shader.
// But the daemon's CAVA RUN-GATE never counts an animation pack either. It asks
// whether the ZONE OVERLAY is displaying or some DECORATION slot carries an
// audio-reactive surface pack, and nothing else. So an audio animation pack on
// an OSD, snap-assist or layout-picker leg reads bar count 0 and renders static
// on the daemon unless one of those two unrelated things happens to be true at
// the same moment. There is no warning at any level.
//
// iAudioSpectrumSize is the bar count, 0 when the visualizer is off or cava is
// unavailable. On the daemon it lives in the AnimationUniforms UBO (declared by
// animation_uniforms.glsl); on the kwin path it is a default-block uniform
// declared HERE, pushed per frame alongside the sampler bind — packs that never
// include this module keep the canonical header's compile-error guard against
// reaching for it.

#ifndef PLASMAZONES_ANIMATION_AUDIO_GLSL
#define PLASMAZONES_ANIMATION_AUDIO_GLSL

// Audio spectrum texture (binding 10 on the daemon's RHI pipeline, shared with
// the overlay convention; a plain named sampler on the compositor's classic-GL
// pipeline, bound to a texture unit at draw time). Never sampled while
// iAudioSpectrumSize is 0. 1D: bar index = x, y = 0; R = bar value in 0..1.
#ifdef PLASMAZONES_KWIN
uniform sampler2D uAudioSpectrum;
uniform int iAudioSpectrumSize;
#else
layout(binding = 10) uniform sampler2D uAudioSpectrum;
// iAudioSpectrumSize comes from the AnimationUniforms UBO.
#endif

// Sample bar value (0..1). Returns 0 if audio is disabled or the index is out
// of range.
float audioBar(int barIndex) {
    if (iAudioSpectrumSize <= 0 || barIndex < 0 || barIndex >= iAudioSpectrumSize) {
        return 0.0;
    }
    return texelFetch(uAudioSpectrum, ivec2(barIndex, 0), 0).r;
}

// Normalized bar index 0..1 for smooth UV sampling.
float audioBarSmooth(float u) {
    if (iAudioSpectrumSize <= 0)
        return 0.0;
    return texture(uAudioSpectrum, vec2(u, 0.5)).r;
}

// ── Frequency-band helpers ───────────────────────────────────────────────────
// The spectrum is NOT one low-to-high block. The shipped default channel mode is
// stereo, and cava emits the left channel's bars low-to-high followed by the right
// channel's (IAudioSpectrumProvider documents that layout, and nothing between
// cava's stdout and the sampler reorders it). Banding the raw vector therefore
// mixed one channel's treble with the other's bass. These helpers fold the two
// channels first, so a band means the same thing in either mode. audioBar() and
// audioBarSmooth() still address the RAW vector, which is what a spectrum-bar
// visualiser wants.

// Per-channel bar count: half the vector in stereo, all of it in the mono modes.
int audioHalf() {
    return (iAudioSpectrumSize >= 2) ? iAudioSpectrumSize / 2 : iAudioSpectrumSize;
}

// Bar `i` of the folded mono spectrum, for 0 <= i < audioHalf().
float audioBarMono(int i) {
    int h = audioHalf();
    if (h == iAudioSpectrumSize)
        return audioBar(i);
    return 0.5 * (audioBar(i) + audioBar(i + h));
}

float getBass() {
    int h = audioHalf();
    if (h <= 0)
        return 0.0;
    // Fractional, like the two bands below. An absolute window made "bass" the
    // bottom half of the vector at the minimum bar count and a thirtieth of it at
    // the maximum, so a settings slider changed which frequencies the band covered.
    int hi = max(h / 8, 1);
    float sum = 0.0;
    for (int i = 0; i < hi; i++)
        sum += audioBarMono(i);
    return sum / float(hi);
}

float getMids() {
    int h = audioHalf();
    if (h <= 0)
        return 0.0;
    int lo = h / 4;
    int hi = h * 3 / 4;
    float sum = 0.0;
    for (int i = lo; i < hi; i++)
        sum += audioBarMono(i);
    return sum / float(max(hi - lo, 1));
}

float getTreble() {
    int h = audioHalf();
    if (h <= 0)
        return 0.0;
    int lo = h * 3 / 4;
    float sum = 0.0;
    for (int i = lo; i < h; i++)
        sum += audioBarMono(i);
    return sum / float(max(h - lo, 1));
}

float getOverall() {
    if (iAudioSpectrumSize <= 0)
        return 0.0;
    float sum = 0.0;
    for (int i = 0; i < iAudioSpectrumSize; i++)
        sum += audioBar(i);
    return sum / float(iAudioSpectrumSize);
}

// ── Dampened band helpers ────────────────────────────────────────────────────
// A noise-floor gate + soft curve so a weak signal doesn't jitter the visual
// while strong hits stay punchy. Use these where audio is an enhancement.

float getBassSoft() {
    float v = getBass();
    return v * smoothstep(0.04, 0.25, v);
}
float getMidsSoft() {
    float v = getMids();
    return v * smoothstep(0.03, 0.20, v);
}
float getTrebleSoft() {
    float v = getTreble();
    return v * smoothstep(0.03, 0.20, v);
}
float getOverallSoft() {
    float v = getOverall();
    return v * smoothstep(0.03, 0.20, v);
}

#endif // PLASMAZONES_ANIMATION_AUDIO_GLSL
