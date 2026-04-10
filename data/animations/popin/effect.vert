// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// PlasmaZones pop-in animation vertex shader
// Scales window from center using pz_style_param as the minimum scale.
// The fragment shader handles the accompanying opacity fade.

uniform mat4 modelViewProjectionMatrix;
uniform float pz_progress;
uniform float pz_style_param; // minScale (default 0.87)
uniform vec2 pz_target_size;

attribute vec4 position;
attribute vec4 texcoord;

varying vec2 texcoord0;

void main() {
    texcoord0 = texcoord.st;

    float minScale = clamp(pz_style_param, 0.1, 1.0);
    float scale = minScale + (1.0 - minScale) * pz_progress;

    // Scale from center of the window
    vec2 center = pz_target_size * 0.5;
    vec2 newPos = center + (position.xy - center) * scale;

    gl_Position = modelViewProjectionMatrix * vec4(newPos, 0.0, 1.0);
}
