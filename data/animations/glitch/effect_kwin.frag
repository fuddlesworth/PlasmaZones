// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Glitch animation — KWin wrapper (GLSL 1.10, individual uniforms)

uniform sampler2D sampler;
varying vec2 texcoord0;

uniform float pz_progress;
uniform float customParams1_x; // bandCount (slot 0)
uniform float customParams1_y; // shiftIntensity (slot 1)
uniform float customParams1_z; // rgbSeparation (slot 2)

#include "glitch.glsl"

void main() {
    float bandCount = customParams1_x > 0.0 ? customParams1_x : 20.0;
    float shiftIntensity = customParams1_y > 0.0 ? customParams1_y : 0.1;
    float rgbSep = customParams1_z > 0.0 ? customParams1_z : 0.02;

    vec2 uv = pzGlitchShift(texcoord0, pz_progress, bandCount, shiftIntensity);
    float sep = pzGlitchSeparation(pz_progress, rgbSep);
    vec2 uvR = clamp(uv + vec2(sep, 0.0), vec2(0.0), vec2(1.0));
    vec2 uvB = clamp(uv - vec2(sep, 0.0), vec2(0.0), vec2(1.0));

    float r = texture2D(sampler, uvR).r;
    vec4 center = texture2D(sampler, uv);
    float b = texture2D(sampler, uvB).b;

    float fade = pzGlitchFade(pz_progress);
    gl_FragColor = vec4(r, center.g, b, center.a) * fade;
}
