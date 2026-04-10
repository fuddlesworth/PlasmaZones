// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// NOTE: This vertex shader is shared across morph/slide/slidefade animations.
// If you modify this, update the others to match.
//
// PlasmaZones slide animation vertex shader
// Translates and scales window geometry from start to target position.
// Identical geometry to morph — the fragment shader handles the opacity fade.
// See also: ../morph/effect.vert, ../slidefade/effect.vert

uniform mat4 modelViewProjectionMatrix;
uniform float pz_progress;
uniform vec2 pz_start_pos;
uniform vec2 pz_start_size;
uniform vec2 pz_target_pos;
uniform vec2 pz_target_size;

attribute vec4 position;
attribute vec4 texcoord;

varying vec2 texcoord0;

void main() {
    texcoord0 = texcoord.st;

    vec2 currentSize = mix(pz_start_size, pz_target_size, pz_progress);
    vec2 currentPos = mix(pz_start_pos, pz_target_pos, pz_progress);

    vec2 offset = currentPos - pz_target_pos;
    vec2 scale = currentSize / max(pz_target_size, vec2(1.0));
    vec2 newPos = position.xy * scale + offset;

    gl_Position = modelViewProjectionMatrix * vec4(newPos, 0.0, 1.0);
}
