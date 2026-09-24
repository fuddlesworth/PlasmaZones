// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Audio spectrum helpers. Include after common.glsl.
// Requires uAudioSpectrum (binding 10) and iAudioSpectrumSize from the UBO.
//
//   #include <common.glsl>
//   #include <audio.glsl>

#ifndef PHOSPHORSHADERS_AUDIO_GLSL
#define PHOSPHORSHADERS_AUDIO_GLSL

// Audio spectrum texture (binding 10). 1D: bar index = x, y=0. R = bar value 0-1.
// Only valid when iAudioSpectrumSize > 0. Include <audio.glsl> for helpers.
layout(binding = 10) uniform sampler2D uAudioSpectrum;

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
    if (h == iAudioSpectrumSize) {
        return audioBar(i);
    }
    return 0.5 * (audioBar(i) + audioBar(i + h));
}

float getBass() {
    int h = audioHalf();
    if (h <= 0) return 0.0;
    // Fractional, like the two bands below. An absolute window made "bass" the
    // bottom half of the vector at the minimum bar count and a thirtieth of it at
    // the maximum, so a settings slider changed which frequencies the band covered.
    int hi = max(h / 8, 1);
    float sum = 0.0;
    for (int i = 0; i < hi; i++) sum += audioBarMono(i);
    return sum / float(hi);
}

float getMids() {
    int h = audioHalf();
    if (h <= 0) return 0.0;
    int lo = h / 4;
    int hi = h * 3 / 4;
    float sum = 0.0;
    for (int i = lo; i < hi; i++) sum += audioBarMono(i);
    return sum / float(max(hi - lo, 1));
}

float getTreble() {
    int h = audioHalf();
    if (h <= 0) return 0.0;
    int lo = h * 3 / 4;
    float sum = 0.0;
    for (int i = lo; i < h; i++) sum += audioBarMono(i);
    return sum / float(max(h - lo, 1));
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
