// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in hashing / value-noise helpers for SURFACE shader packs. `#include
// <surface_noise.glsl>` only in packs that need them (frost / rain / circuit /
// firefly grain). Two hash families are exposed deliberately:
//
//   • hash13 / hash23 — Dave-Hoskins integer hashes, driver-stable (no sin()),
//     the default for grain that must look identical across GPUs.
//   • hashSin / hashSin1 — the classic sin()-based hashes. These vary per
//     driver; they are kept as SEPARATE symbols because a few packs (glass
//     grain, firefly placement) are visually tuned to their specific output and
//     must NOT be silently swapped onto the integer hash.

#ifndef PLASMAZONES_SURFACE_NOISE_GLSL
#define PLASMAZONES_SURFACE_NOISE_GLSL

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

// Crystalline Voronoi noise: the gap between the two nearest cell-point
// distances gives frost-crystal boundaries.
float voronoi(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 1.0;
    float secondMin = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 cellId = i + neighbor;
            float h1 = hash13(cellId);
            float h2 = hash13(cellId + vec2(127.1, 311.7));
            vec2 diff = neighbor + vec2(h1, h2) - f;
            float dist = dot(diff, diff);
            if (dist < minDist) {
                secondMin = minDist;
                minDist = dist;
            } else if (dist < secondMin) {
                secondMin = dist;
            }
        }
    }
    return sqrt(secondMin) - sqrt(minDist);
}

// Hex-grid metric: distance to the nearest hex-cell edge.
float hexDist(vec2 p) {
    p = abs(p);
    return max(p.x * 0.866025 + p.y * 0.5, p.y);
}

// Hex-grid cell-local offset for a point in a unit hex lattice.
vec2 hexLocal(vec2 uv) {
    vec2 r = vec2(1.0, 1.732);
    vec2 h = r * 0.5;
    vec2 a = mod(uv, r) - h;
    vec2 b = mod(uv - h, r) - h;
    return dot(a, a) < dot(b, b) ? a : b;
}

// Classic sin()-based hashes — DRIVER-VARIANT. Use only where a pack's look is
// tuned to this specific distribution (see the file header).
float hashSin(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}
float hashSin1(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

#endif // PLASMAZONES_SURFACE_NOISE_GLSL
