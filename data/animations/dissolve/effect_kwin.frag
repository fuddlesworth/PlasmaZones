// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Dissolve animation — KWin wrapper (GLSL 1.10, individual uniforms)

uniform sampler2D sampler;
varying vec2 texcoord0;

uniform float pz_progress;
uniform float customParams1_x; // noiseScale (slot 0)
uniform float customParams1_y; // edgeSoftness (slot 1)

#include "dissolve.glsl"

void main() {
    float noiseScale = customParams1_x > 0.0 ? customParams1_x : 200.0;
    float edgeSoftness = customParams1_y > 0.0 ? customParams1_y : 0.05;

    vec4 tex = texture2D(sampler, texcoord0);
    float alpha = pzDissolveAlpha(texcoord0, pz_progress, noiseScale, edgeSoftness);
    gl_FragColor = tex * alpha;
}
