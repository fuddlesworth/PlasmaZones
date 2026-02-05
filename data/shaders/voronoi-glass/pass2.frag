// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Voronoi Stained Glass — Buffer Pass 2: Vertical Bloom + Composite
// Completes the separable Gaussian bloom blur (vertical pass on iChannel1),
// composites bloom with the original 3D scene from iChannel0, and applies
// tone mapping with a subtle vignette.
// Output to iChannel2: RGB = final post-processed image, A = 1.0.

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>

void main() {
    vec2 fragCoord = fragCoordFromTexCoord(vTexCoord);
    vec2 uv1 = channelUv(1, fragCoord);
    float py = 1.0 / max(iChannelResolution[1].y, 1.0);

    // 11-tap vertical Gaussian matching pass 1
    const int TAPS = 6;
    float w[TAPS] = float[](0.1974, 0.1747, 0.1210, 0.0656, 0.0278, 0.0092);

    vec3 bloom = texture(iChannel1, uv1).rgb * w[0];
    for (int i = 1; i < TAPS; i++) {
        float off = float(i) * py * 2.0;
        bloom += texture(iChannel1, uv1 + vec2(0.0, off)).rgb * w[i];
        bloom += texture(iChannel1, uv1 - vec2(0.0, off)).rgb * w[i];
    }

    // Original 3D scene
    vec3 scene = texture(iChannel0, channelUv(0, fragCoord)).rgb;

    // Bloom composite (warm cathedral glow)
    vec3 col = scene + bloom * 0.45;

    // Reinhard tone mapping
    col = col / (col + 1.0);

    // Subtle vignette
    vec2 vuv = fragCoord / max(iResolution.xy, vec2(1.0)) - 0.5;
    col *= 1.0 - dot(vuv, vuv) * 0.2;

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
