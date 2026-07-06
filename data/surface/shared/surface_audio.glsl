// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in audio-spectrum helpers for SURFACE shader packs — the decoration
// cousin of the overlay category's data/shaders/shared/audio.glsl. `#include
// <surface_audio.glsl>` in a pack that reacts to the CAVA spectrum, then guard
// on the helpers (they return 0 when audio is off).
//
// The spectrum is a session-global feature, exactly as it is for zone packs:
// the daemon pushes the same spectrum to every surface item when the audio
// visualizer is enabled, and a pack opts in purely by including this file and
// reading the helpers. iAudioSpectrumSize (from the uniform contract) is the
// bar count, 0 when audio is disabled. The compositor wires no audio texture,
// so window decorations always read 0 here — audio-reactive decoration is a
// daemon-surface (OSD / popup) feature.

#ifndef PLASMAZONES_SURFACE_AUDIO_GLSL
#define PLASMAZONES_SURFACE_AUDIO_GLSL

#include <surface_uniforms.glsl>

// Audio spectrum texture (binding 6 on the daemon; a plain unbound sampler on
// the compositor, where iAudioSpectrumSize is 0 so it is never sampled). 1D:
// bar index = x, y = 0; R = bar value in 0..1.
#ifdef PLASMAZONES_KWIN
uniform sampler2D uAudioSpectrum;
#else
layout(binding = 6) uniform sampler2D uAudioSpectrum;
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

#endif // PLASMAZONES_SURFACE_AUDIO_GLSL
