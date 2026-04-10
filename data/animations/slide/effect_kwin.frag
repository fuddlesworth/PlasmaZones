// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Slide animation — KWin wrapper (opacity fade over first 30%, geometry by C++)

uniform sampler2D sampler;
varying vec2 texcoord0;
uniform float pz_progress;

void main() {
    vec4 tex = texture2D(sampler, texcoord0);
    float opacity = clamp(pz_progress / 0.3, 0.0, 1.0);
    gl_FragColor = tex * opacity;
}
