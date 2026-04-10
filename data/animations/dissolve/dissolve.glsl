// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Dissolve effect logic — shared between QML and KWin wrappers.
// Pure function: no uniform/texture references, all inputs as parameters.

float pzDissolveHash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

/// Returns alpha multiplier for the dissolve effect.
/// @param uv Texture coordinates [0-1]
/// @param progress Animation progress [0-1] (0=fully visible, 1=fully dissolved)
/// @param noiseScale Scale of the noise pattern (default 200)
/// @param edgeSoftness Smoothstep edge width (default 0.05)
float pzDissolveAlpha(vec2 uv, float progress, float noiseScale, float edgeSoftness) {
    float noise = pzDissolveHash(uv * noiseScale);
    float threshold = progress;
    return smoothstep(threshold - edgeSoftness, threshold + edgeSoftness, 1.0 - noise);
}
