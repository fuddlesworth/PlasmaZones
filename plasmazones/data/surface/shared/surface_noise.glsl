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
//
// INPUT DOMAIN, which these helpers do not enforce and a third-party pack has
// to respect. All thresholds below are computed from IEEE-754 binary32, not
// measured on a driver, and none of them is reachable from a pack driving
// these off a fragment coordinate at any real surface size. They matter for a
// pack that feeds in accumulated time or an unbounded world coordinate.
//
//   • hash13 / hash23 stay in [0, 1) for every finite input, never NaN and
//     never negative, but adjacent integer cells stop hashing differently once
//     the ULP of p.x * 0.1031 reaches 0.1031, around |p| > 8.3e6: the grain
//     goes flat rather than wrong.
//   • vnoise and voronoi floor() and add 1.0, so they collapse to a constant
//     at |p| >= 2^24.
//   • hexLocal is the only path that can produce a NaN, because mod() is a
//     cancelling subtraction quantised to ULP(uv); it stair-steps well before
//     that. Keep its input bounded.
//   • Both integer hashes return exactly 0 at the EXACT ORIGIN, p == vec2(0),
//     and only there. A hash result reads like a value in (0, 1), so using one
//     as a divisor, a pow base or a smoothstep edge is idiomatic and each is
//     degenerate at 0. No bundled caller does; a third-party one should offset
//     its input or bias the result.

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

// Crystalline Voronoi noise, giving frost-crystal boundaries.
//
// Returns min(1.0, F2) - F1 rather than the plain F2 - F1 gap, because
// secondMin is seeded to the SAME 1.0 as minDist. Any second-nearest point
// farther than one cell width is therefore never recorded and the seed stands
// as a ceiling, which with one jittered point per cell happens over a
// substantial share of the plane. The result is the intended look and is the
// one the packs are tuned against, so the ceiling is kept; the two-minimum
// invariant secondMin >= minDist holds on both branches, so the sqrt below
// never sees a negative and the return stays in [0, 1].
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

// The family's grain term: driver-stable noise in [-1, 1] scaled by @p strength,
// which is clamped to the 0.2 ceiling every pack's Noise parameter declares.
//
// Written out identically in five packs (blur, duotone, phosphor-glass,
// rain-glass, rippled-glass), each repeating that 0.2. The ceiling is the thing
// worth having in one place: it is a rendering decision about how much grain is
// too much, and five copies of it drift one at a time.
//
// GLASS DELIBERATELY DOES NOT USE THIS. Its grain is tuned to hashSin's specific
// per-driver output, which the header above keeps as a separate symbol precisely
// so it cannot be silently swapped onto the integer hash. It keeps its own line
// and its own comment saying why.
//
// Returns the UN-weighted term. A pack over a premultiplied backdrop multiplies
// by that alpha itself, which four of the five do and blur does not need to,
// since it grains an un-premultiplied colour and re-premultiplies after.
float surfaceGrain(vec2 px, float strength) {
    return (hash13(px) - 0.5) * 2.0 * clamp(strength, 0.0, 0.2);
}

#endif // PLASMAZONES_SURFACE_NOISE_GLSL
