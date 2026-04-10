// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Glitch effect logic — shared between QML and KWin wrappers.
// Pure functions: no uniform/texture references, all inputs as parameters.

float pzGlitchRand(float seed) {
    return fract(sin(seed * 78.233) * 43758.5453);
}

/// Returns UV with horizontal glitch shift applied.
/// @param uv Original texture coordinates [0-1]
/// @param progress Animation progress [0-1]
/// @param bandCount Number of horizontal glitch bands (default 20)
/// @param shiftIntensity Maximum horizontal shift (default 0.1)
vec2 pzGlitchShift(vec2 uv, float progress, float bandCount, float shiftIntensity) {
    float bandY = floor(uv.y * bandCount) / bandCount;
    float shift = (pzGlitchRand(bandY + progress * 7.0) - 0.5) * shiftIntensity * progress;
    return clamp(uv + vec2(shift, 0.0), vec2(0.0), vec2(1.0));
}

/// Returns RGB separation offset.
float pzGlitchSeparation(float progress, float rgbSep) {
    return progress * rgbSep;
}

/// Returns fade multiplier for glitch.
float pzGlitchFade(float progress) {
    return 1.0 - smoothstep(0.6, 1.0, progress);
}
