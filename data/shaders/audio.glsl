// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audio spectrum helpers. Include after common.glsl.
// Requires uAudioSpectrum (binding 6) and iAudioSpectrumSize from ZoneUniforms.
//
//   #include <common.glsl>
//   #include <audio.glsl>

#ifndef PLASMAZONES_AUDIO_GLSL
#define PLASMAZONES_AUDIO_GLSL

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

#endif
