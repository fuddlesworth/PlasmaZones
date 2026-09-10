// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in hashing / value-noise helpers for POINTER shader packs. `#include
// <pointer_noise.glsl>` only in packs that need them (comet sparkle, spark
// launch directions). Two hash families are exposed deliberately, matching
// data/surface/shared/surface_noise.glsl:
//
//   • hash13 / hash23 — Dave-Hoskins integer hashes, driver-stable (no sin()),
//     the default for anything that must look identical across GPUs.
//   • hashSin / hashSin1 — the classic sin()-based hashes. These vary per
//     driver and are kept as SEPARATE symbols for packs tuned to their output.
//
// The set mirrors surface_noise.glsl helper for helper, so a shader author
// moving between the families finds the same names, and a third-party pack
// can rely on every one of them being there whether or not a bundled pack
// happens to use it (vnoise and hashSin currently have no bundled consumer).

#ifndef PLASMAZONES_POINTER_NOISE_GLSL
#define PLASMAZONES_POINTER_NOISE_GLSL

// Dave-Hoskins vec2 -> float, driver-stable.
float hash13(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Dave-Hoskins vec2 -> vec3, driver-stable.
vec3 hash23(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xxy + p3.yzz) * p3.zyx);
}

// Quintic-interpolated value noise over the integer hash.
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash13(i);
    float b = hash13(i + vec2(1.0, 0.0));
    float c = hash13(i + vec2(0.0, 1.0));
    float d = hash13(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Classic sin()-based hashes — DRIVER-VARIANT. Use only where a pack's look is
// tuned to this specific distribution (see the file header).
float hashSin(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}
float hashSin1(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

#endif // PLASMAZONES_POINTER_NOISE_GLSL
