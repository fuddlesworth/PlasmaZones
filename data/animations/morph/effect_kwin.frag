// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Morph animation — KWin wrapper (passthrough, geometry handled by C++)

uniform sampler2D sampler;
varying vec2 texcoord0;

void main() {
    gl_FragColor = texture2D(sampler, texcoord0);
}
