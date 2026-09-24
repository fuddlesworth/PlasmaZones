// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audio spectrum helpers. Include after common.glsl.
// Requires uAudioSpectrum (binding 10) and iAudioSpectrumSize from ZoneUniforms.
//
//   #include <common.glsl>
//   #include <audio.glsl>

#ifndef PLASMAZONES_AUDIO_GLSL
#define PLASMAZONES_AUDIO_GLSL

// Audio spectrum texture (binding 10). 1D: bar index = x, y=0. R = bar value 0-1.
// Only valid when iAudioSpectrumSize > 0; the helpers below all return 0 when
// it is not.
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

// ── Fetch budget ─────────────────────────────────────────────────────────────
// Every band helper below runs PER FRAGMENT, so its loop bound is a per-pixel
// cost. Walking every bar made that cost scale with a SETTING: at the widest bar
// counts the four helpers together reached several hundred texelFetch per
// fragment, and a pack calling more than one of them paid it more than once.
//
// The band is sampled at a bounded number of evenly spaced taps instead. Each
// tap is one audioBarMono, which is two fetches in stereo, so a band costs at
// most about 2 * kAudioBandTaps fetches whatever the bar count.
//
// AT SMALL BAR COUNTS NOTHING CHANGES. The stride is max(n / taps, 1), so a band
// narrower than the tap budget still walks every bar and returns the exact mean
// it always did. The approximation appears only where the exact answer was
// unaffordable, and a band mean over a smooth spectrum is what these helpers
// exist to give.
const int kAudioBandTaps = 8;

// Mean of the FOLDED spectrum over [lo, hi), at no more than kAudioBandTaps
// evenly spaced taps.
float audioBandMean(int lo, int hi) {
    int n = hi - lo;
    if (n <= 0)
        return 0.0;
    int stride = max(n / kAudioBandTaps, 1);
    float sum = 0.0;
    int taps = 0;
    for (int i = lo; i < hi; i += stride) {
        sum += audioBarMono(i);
        taps++;
    }
    return taps > 0 ? sum / float(taps) : 0.0;
}

float getBass() {
    int h = audioHalf();
    if (h <= 0) return 0.0;
    // Fractional, like the two bands below. An absolute window made "bass" the
    // bottom half of the vector at the minimum bar count and a thirtieth of it at
    // the maximum, so a settings slider changed which frequencies the band covered.
    return audioBandMean(0, max(h / 8, 1));
}

float getMids() {
    int h = audioHalf();
    if (h <= 0) return 0.0;
    return audioBandMean(h / 4, h * 3 / 4);
}

float getTreble() {
    int h = audioHalf();
    if (h <= 0) return 0.0;
    return audioBandMean(h * 3 / 4, h);
}

// Strided over the RAW vector, not the folded one: a full mean is correct at
// either channel layout, so there is nothing to fold. Twice a single band's tap
// budget, since this covers the whole spectrum and each tap is a single fetch
// rather than a folded pair.
float getOverall() {
    if (iAudioSpectrumSize <= 0)
        return 0.0;
    int stride = max(iAudioSpectrumSize / (kAudioBandTaps * 2), 1);
    float sum = 0.0;
    int taps = 0;
    for (int i = 0; i < iAudioSpectrumSize; i += stride) {
        sum += audioBar(i);
        taps++;
    }
    return taps > 0 ? sum / float(taps) : 0.0;
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
