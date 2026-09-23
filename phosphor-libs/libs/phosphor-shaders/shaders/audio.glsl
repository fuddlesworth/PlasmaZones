// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Audio spectrum helpers. Include after common.glsl.
// Requires uAudioSpectrum (binding 6) and iAudioSpectrumSize from the UBO.
//
//   #include <common.glsl>
//   #include <audio.glsl>

#ifndef PHOSPHORSHADERS_AUDIO_GLSL
#define PHOSPHORSHADERS_AUDIO_GLSL

// Audio spectrum texture (binding 6). 1D: bar index = x, y=0. R = bar value 0-1.
// Only valid when iAudioSpectrumSize > 0. Include <audio.glsl> for helpers.
layout(binding = 6) uniform sampler2D uAudioSpectrum;

// Sample bar value (0-1). Returns 0 if audio disabled or index out of range.
float audioBar(int barIndex) {
    if (iAudioSpectrumSize <= 0 || barIndex < 0 || barIndex >= iAudioSpectrumSize) {
        return 0.0;
    }
    return texelFetch(uAudioSpectrum, ivec2(barIndex, 0), 0).r;
}

// Normalized bar index 0-1 for UV sampling (smooth interpolation).
float audioBarSmooth(float u) {
    if (iAudioSpectrumSize <= 0) return 0.0;
    return texture(uAudioSpectrum, vec2(u, 0.5)).r;
}

// ─── Frequency band helpers ──────────────────────────────────────────────────

float getBass() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    int n = min(iAudioSpectrumSize, 8);
    for (int i = 0; i < n; i++) sum += audioBar(i);
    return sum / float(n);
}

float getMids() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    int lo = iAudioSpectrumSize / 4;
    int hi = iAudioSpectrumSize * 3 / 4;
    for (int i = lo; i < hi && i < iAudioSpectrumSize; i++) sum += audioBar(i);
    return sum / float(max(hi - lo, 1));
}

float getTreble() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    int lo = iAudioSpectrumSize * 3 / 4;
    for (int i = lo; i < iAudioSpectrumSize; i++) sum += audioBar(i);
    return sum / float(max(iAudioSpectrumSize - lo, 1));
}

float getOverall() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    for (int i = 0; i < iAudioSpectrumSize; i++) sum += audioBar(i);
    return sum / float(iAudioSpectrumSize);
}

// ─── Dampened band helpers (for non-audio-primary shaders) ───────────────────
// Apply noise-floor gate + power curve to suppress jitter from weak signals
// while keeping strong hits punchy. Use these instead of raw getBass()/etc.
// in shaders where audio is an enhancement, not the core visual.

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

#endif
