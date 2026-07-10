// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PlasmaZones shared flow-field helpers (GLSL #version 450).
// Include after common.glsl:
//   #include <common.glsl>
//   #include <flow-noise.glsl>
//
// Kept opt-in rather than in the auto-prepended common.glsl: curl noise is a
// flow-field-specific helper only the flow packs use, and keeping the
// signature out of every effect shader's translation unit leaves packs free
// to carry their own variants (opensuse-drift's forward-difference version is
// named curlNoiseFwd to stay collision-proof either way).

#ifndef PLASMAZONES_FLOW_NOISE_GLSL
#define PLASMAZONES_FLOW_NOISE_GLSL

#include <common.glsl>

// Single-octave curl noise — 4 noise2D calls (vs 12-16 in multi-octave)
vec2 curlNoise(vec2 p, float t) {
    float eps = 0.5;
    float n  = noise2D(p + vec2(0.0, eps) + t);
    float ns = noise2D(p - vec2(0.0, eps) + t);
    float ne = noise2D(p + vec2(eps, 0.0) + t);
    float nw = noise2D(p - vec2(eps, 0.0) + t);
    return vec2(n - ns, -(ne - nw)) / (2.0 * eps);
}

#endif // PLASMAZONES_FLOW_NOISE_GLSL
