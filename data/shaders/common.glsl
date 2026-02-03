// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PlasmaZones shared shader helpers (Vulkan GLSL #version 450).
// Include from effect.frag or zone.vert with:
//   #include <common.glsl>   (from global shaders dir)
//   #include "common.glsl"   (from current shader dir if copied locally)
//
// The main shader must declare the ZoneUniforms block; this file only provides
// helper functions.

// Signed distance to rounded box (half-extents b, corner radius r)
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Pseudo-random: 1D in → 1D out (float → float)
float hash11(float n) {
    return fract(sin(n) * 43758.5453123);
}

// Pseudo-random: 2D in → 1D out (vec2 → float)
float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Pseudo-random: 2D in → 2D out (vec2 → vec2, e.g. for particle positions)
vec2 hash22(vec2 p) {
    vec2 k = vec2(0.3183099, 0.3678794);
    p = p * k + k.yx;
    return fract(sin(vec2(p.x * p.y, p.y * p.x)) * 43758.5453);
}
