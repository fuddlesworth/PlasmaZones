// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pixelate animation — KWin wrapper (GLSL 1.10, individual uniforms)

uniform sampler2D sampler;
varying vec2 texcoord0;

uniform float pz_progress;
uniform int textureWidth;
uniform int textureHeight;
uniform float customParams1_x; // maxPixelSize (slot 0)
uniform float customParams1_y; // fadeStart (slot 1)

#include "pixelate.glsl"

void main() {
    if (textureWidth <= 0 || textureHeight <= 0) {
        gl_FragColor = texture2D(sampler, texcoord0);
        return;
    }

    float maxPixelSize = customParams1_x > 0.0 ? customParams1_x : 40.0;
    float fadeStart = customParams1_y > 0.0 ? customParams1_y : 0.7;

    vec2 resolution = vec2(float(textureWidth), float(textureHeight));
    vec2 coord = pzPixelateCoord(texcoord0, resolution, pz_progress, maxPixelSize);
    vec4 tex = texture2D(sampler, coord);
    float fade = pzPixelateFade(pz_progress, fadeStart);
    gl_FragColor = tex * fade;
}
