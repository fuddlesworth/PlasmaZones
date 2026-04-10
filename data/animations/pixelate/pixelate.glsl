// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pixelate effect logic — shared between QML and KWin wrappers.
// Pure functions: no uniform/texture references, all inputs as parameters.

/// Returns pixelated UV coordinates.
/// @param uv Original texture coordinates [0-1]
/// @param resolution Texture size in pixels
/// @param progress Animation progress [0-1]
/// @param maxPixelSize Maximum pixel block size (default 40)
vec2 pzPixelateCoord(vec2 uv, vec2 resolution, float progress, float maxPixelSize) {
    float pixelSize = mix(1.0, maxPixelSize, progress);
    return floor(uv * resolution / pixelSize) * pixelSize / resolution;
}

/// Returns fade multiplier for pixelate.
/// @param progress Animation progress [0-1]
/// @param fadeStart Progress at which fade begins (default 0.7)
float pzPixelateFade(float progress, float fadeStart) {
    return 1.0 - smoothstep(fadeStart, 1.0, progress);
}
