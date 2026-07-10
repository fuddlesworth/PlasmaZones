// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PlasmaZones shared helpers for the logo "drift" shader family (GLSL #version
// 450). Include after common.glsl:
//   #include <common.glsl>
//   #include <logo-drift.glsl>
//
// Holds the byte-identical building blocks shared across the drift packs: the
// simplex-noise stack (arch, endeavouros), Catmull-Rom interpolation plus the
// logoPaletteCR/paletteSweep palette sweep built on it (arch, endeavouros),
// and the neon flicker envelope (fedora, neon). The per-pack instance
// placement (computeInstanceUV) and the per-zone composite loops stay in each
// pack because they diverge algorithmically.

#ifndef PLASMAZONES_LOGO_DRIFT_GLSL
#define PLASMAZONES_LOGO_DRIFT_GLSL

#include <common.glsl>

// ─── Simplex noise ───────────────────────────────────────────────────────────
// Ashima-style 2D simplex noise. Kept separate from common.glsl's value noise
// (noise2D) because only the arch/endeavouros packs need the gradient variety.

vec3 simplexMod289(vec3 x) { return x - floor(x / 289.0) * 289.0; }
vec2 simplexMod289v2(vec2 x) { return x - floor(x / 289.0) * 289.0; }
vec3 simplexPermute(vec3 x) { return simplexMod289((x * 34.0 + 1.0) * x); }

float simplex2D(vec2 v) {
    const vec4 C = vec4(0.211324865405, 0.366025403784,
                        -0.577350269189, 0.024390243902);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = simplexMod289v2(i);
    vec3 p = simplexPermute(simplexPermute(i.y + vec3(0.0, i1.y, 1.0))
                            + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy),
                             dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;
    vec3 x_ = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x_) - 0.5;
    vec3 ox = floor(x_ + 0.5);
    vec3 a0 = x_ - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

float simplexFBM(vec2 uv, int octaves) {
    float value = 0.0;
    float amplitude = 0.6;
    float freq = 1.0;
    for (int i = 0; i < octaves && i < 8; i++) {
        value += amplitude * (simplex2D(uv * freq) * 0.5 + 0.5);
        freq *= 2.1;
        amplitude *= 0.45;
    }
    return value;
}

// ─── Catmull-Rom spline (arch, endeavouros palettes) ─────────────────────────
vec3 catmullRom(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5 * ((2.0 * p1) +
                   (-p0 + p2) * t +
                   (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                   (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

// ─── Neon flicker envelope (fedora, neon) ────────────────────────────────────
float neonFlicker(float time, float seed, float trebleEnv) {
    float base = 0.94 + 0.06 * sin(time * 8.0 + seed * 100.0);
    float buzz = step(0.97, noise2D(vec2(time * 4.0, seed * 7.0))) * 0.2;
    float trebleBuzz = trebleEnv * step(0.93, noise2D(vec2(time * 5.0, seed * 13.0))) * 0.25;
    return clamp(base - buzz - trebleBuzz, 0.6, 1.0);
}

// ─── Catmull-Rom palette sweep (arch, endeavouros) ───────────────────────────
// Smooth wrap-around sweep through primary -> secondary -> accent -> glow ->
// primary via catmullRom, plus the audio-shifted convenience wrapper.
vec3 logoPaletteCR(float t, vec3 primary, vec3 secondary, vec3 accent, vec3 glow) {
    t = fract(t);
    float seg = t * 4.0;
    int idx = int(seg);
    float f = fract(seg);
    vec3 colors[5] = vec3[5](primary, secondary, accent, glow, primary);
    int i0 = max(idx - 1, 0);
    int i1 = idx;
    int i2 = min(idx + 1, 4);
    int i3 = min(idx + 2, 4);
    return clamp(catmullRom(colors[i0], colors[i1], colors[i2], colors[i3], f), 0.0, 1.0);
}

vec3 paletteSweep(float t, vec3 primary, vec3 secondary, vec3 accent, vec3 glow,
                  float audioShift) {
    // Subtle hue shift from audio stays within the pack's color family
    float shifted = t + audioShift * 0.08;
    return logoPaletteCR(shifted, primary, secondary, accent, glow);
}

#endif // PLASMAZONES_LOGO_DRIFT_GLSL
