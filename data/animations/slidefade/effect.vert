// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// NOTE: This vertex shader is shared across morph/slide/slidefade animations.
// If you modify this, update the others to match.
//
// PlasmaZones slide-fade animation vertex shader
// Partial translate: pz_style_param controls how much of the full translate
// distance is applied (0 = no slide, 1 = full morph slide).
// The fragment shader handles the accompanying opacity blend.
// See also: ../morph/effect.vert, ../slide/effect.vert

uniform mat4 modelViewProjectionMatrix;
uniform float pz_progress;
uniform float pz_style_param; // slideFraction (default 0.87)
uniform vec2 pz_start_pos;
uniform vec2 pz_start_size;
uniform vec2 pz_target_pos;
uniform vec2 pz_target_size;

attribute vec4 position;
attribute vec4 texcoord;

varying vec2 texcoord0;

void main() {
    texcoord0 = texcoord.st;

    float slideFraction = clamp(pz_style_param, 0.0, 1.0);

    vec2 currentSize = mix(pz_start_size, pz_target_size, pz_progress);
    vec2 currentPos = mix(pz_start_pos, pz_target_pos, pz_progress);

    // Apply only a fraction of the translate offset
    vec2 fullOffset = currentPos - pz_target_pos;
    vec2 offset = fullOffset * slideFraction;
    vec2 scale = currentSize / max(pz_target_size, vec2(1.0));
    vec2 newPos = position.xy * scale + offset;

    gl_Position = modelViewProjectionMatrix * vec4(newPos, 0.0, 1.0);
}
