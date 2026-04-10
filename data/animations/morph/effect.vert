// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// NOTE: This vertex shader is shared across morph/slide/slidefade animations.
// If you modify this, update the others to match.
//
// PlasmaZones morph animation vertex shader
// Smoothly interpolates window position and size between start and target.
// See also: ../slide/effect.vert, ../slidefade/effect.vert

uniform mat4 modelViewProjectionMatrix;
uniform float pz_progress;
uniform vec2 pz_start_pos;   // Start top-left in pixels
uniform vec2 pz_start_size;  // Start size in pixels
uniform vec2 pz_target_pos;  // Target top-left in pixels
uniform vec2 pz_target_size; // Target size in pixels

attribute vec4 position;
attribute vec4 texcoord;

varying vec2 texcoord0;

void main() {
    texcoord0 = texcoord.st;

    // Vertex positions are in target window-local space (0,0 → target_size).
    // Compute the normalized position within the window (0-1).
    vec2 uv = position.xy / max(pz_target_size, vec2(1.0));

    // Interpolate size from start to target
    vec2 currentSize = mix(pz_start_size, pz_target_size, pz_progress);

    // Interpolate position from start to target
    vec2 currentPos = mix(pz_start_pos, pz_target_pos, pz_progress);

    // Map vertex to interpolated geometry, offset from target position
    vec2 offset = currentPos - pz_target_pos;
    vec2 scale = currentSize / max(pz_target_size, vec2(1.0));
    vec2 newPos = position.xy * scale + offset;

    gl_Position = modelViewProjectionMatrix * vec4(newPos, 0.0, 1.0);
}
