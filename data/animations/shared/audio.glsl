// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in audio-spectrum helpers for ANIMATION shader packs — the transition
// cousin of surface_audio.glsl (surface family) and data/shaders/shared/
// audio.glsl (overlay family). `#include <audio.glsl>` AFTER
// animation_uniforms.glsl in a pack that reacts to the CAVA spectrum, declare
// `"audio": true` in the pack's metadata.json, and guard on the helpers (they
// return 0 when audio is off).
//
// Unlike the overlay family, where audio is implicit/session-global, animation
// packs must declare the metadata flag: the kwin-effect keys its CAVA run-gate
// on it, keeping the provider warm while an audio pack is assigned anywhere in
// the shader profile tree so a transition's FIRST frame already has a spectrum
// (cava spawn latency would otherwise eat a whole open/close leg). The daemon
// path needs no flag — SurfaceAnimator::setAudioSpectrum feeds every attached
// animation shader.
//
// iAudioSpectrumSize is the bar count, 0 when the visualizer is off or cava is
// unavailable. On the daemon it lives in the AnimationUniforms UBO (declared by
// animation_uniforms.glsl); on the kwin path it is a default-block uniform
// declared HERE, pushed per frame alongside the sampler bind — packs that never
// include this module keep the canonical header's compile-error guard against
// reaching for it.

#ifndef PLASMAZONES_ANIMATION_AUDIO_GLSL
#define PLASMAZONES_ANIMATION_AUDIO_GLSL

// Audio spectrum texture (binding 6 on the daemon's RHI pipeline, shared with
// the overlay convention; a plain named sampler on the compositor's classic-GL
// pipeline, bound to a texture unit at draw time). Never sampled while
// iAudioSpectrumSize is 0. 1D: bar index = x, y = 0; R = bar value in 0..1.
#ifdef PLASMAZONES_KWIN
uniform sampler2D uAudioSpectrum;
uniform int iAudioSpectrumSize;
#else
layout(binding = 6) uniform sampler2D uAudioSpectrum;
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

float getBass() {
    if (iAudioSpectrumSize <= 0)
        return 0.0;
    float sum = 0.0;
    int n = min(iAudioSpectrumSize, 8);
    for (int i = 0; i < n; i++)
        sum += audioBar(i);
    return sum / float(n);
}

float getMids() {
    if (iAudioSpectrumSize <= 0)
        return 0.0;
    float sum = 0.0;
    int lo = iAudioSpectrumSize / 4;
    int hi = iAudioSpectrumSize * 3 / 4;
    for (int i = lo; i < hi && i < iAudioSpectrumSize; i++)
        sum += audioBar(i);
    return sum / float(max(hi - lo, 1));
}

float getTreble() {
    if (iAudioSpectrumSize <= 0)
        return 0.0;
    float sum = 0.0;
    int lo = iAudioSpectrumSize * 3 / 4;
    for (int i = lo; i < iAudioSpectrumSize; i++)
        sum += audioBar(i);
    return sum / float(max(iAudioSpectrumSize - lo, 1));
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
