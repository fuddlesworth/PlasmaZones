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
// the neon flicker envelope (fedora, neon), the drifting per-instance logo
// placement (driftInstanceUV — fedora, neon; parameterized by the recentre
// point and tilt magnitude), and the 5x5 chromatic label-halo gather
// (gatherLabelHalo — fedora, neon). The per-zone composite loops stay in each
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

// ─── Drifting per-instance logo placement (fedora, neon) ─────────────────────
// Per-instance UV for the multi-logo packs: gentle drift, per-instance rotation
// oscillation, breathing, and a bass-driven spring on the scale. `center` is
// the recentre point (LOGO_CENTER / GEAR_CENTER, both vec2(0.5)) and `tiltAmt`
// scales the rotation oscillation (fedora keeps the "f" upright at 0.01, neon
// spins gears harder at 0.05).
vec2 driftInstanceUV(int idx, int totalCount, vec2 globalUV, float aspect, float time,
                     float logoScale, float bassEnv, float sizeMin, float sizeMax,
                     vec2 center, float tiltAmt, out float instScale) {
    vec2 uv = globalUV;
    uv.x = (uv.x - 0.5) * aspect + 0.5;

    if (totalCount <= 1) {
        vec2 drift = vec2(
            timeSin(0.13) * 0.015 + timeSin(0.29) * 0.008,
            timeCos(0.19) * 0.012 + timeCos(0.11) * 0.006
        );
        uv -= drift;
        // Gentle rotation
        float rotAng = timeSin(0.12) * 0.04;
        vec2 lp = uv - vec2(0.5);
        uv = lp * rot(rotAng) + vec2(0.5);
        float breathe = 1.0 + timeSin(0.6) * 0.02;
        float springT = fract(time * 1.2);
        float spring = 1.0 + bassEnv * 0.12 * exp(-springT * 5.0) * cos(springT * 18.0);
        instScale = logoScale * breathe * spring;
        uv = (uv - 0.5) / instScale + center;
        return uv;
    }

    float h1 = hash21(vec2(float(idx) * 7.31, 3.17));
    float h2 = hash21(vec2(float(idx) * 13.71, 7.23));
    float h3 = hash21(vec2(float(idx) * 5.13, 11.37));
    float h4 = hash21(vec2(float(idx) * 9.77, 17.53));

    float roam = 0.35;
    float f1 = 0.06 + float(idx) * 0.021;
    float f2 = 0.04 + float(idx) * 0.017;
    vec2 drift = vec2(
        timeSin(f1, h1 * TAU) * roam + timeSin(f1 * 2.1, h3 * TAU) * roam * 0.3,
        timeCos(f2, h2 * TAU) * roam * 0.9 + timeCos(f2 * 1.6, h4 * TAU) * roam * 0.25
    );
    uv -= drift;

    // Per-instance rotation oscillation (tiltAmt sets the magnitude per pack)
    float rotAng = timeSin(0.08 + float(idx) * 0.025, h4 * TAU) * tiltAmt;
    vec2 lp = uv - vec2(0.5);
    uv = lp * rot(rotAng) + vec2(0.5);

    instScale = mix(sizeMin, sizeMax, h3) * logoScale;
    float breathe = 1.0 + timeSin(0.5 + float(idx) * 0.11, h1 * TAU) * 0.02;
    float springT = fract(time * 1.2 + h2);
    float spring = 1.0 + bassEnv * 0.12 * exp(-springT * 5.0) * cos(springT * 18.0);
    instScale *= breathe * spring;
    uv = (uv - 0.5) / instScale + center;
    return uv;
}

// ─── Chromatic label-halo gather (fedora, neon) ──────────────────────────────
// 5x5 weighted gather over the zone-labels texture (uZoneLabels, from
// common.glsl): a tight core, wide and very-wide haze, plus three chromatically
// offset channels for the retro aberration. `chromOff` is the per-pack channel
// separation; everything else (weights, normalizations) is shared.
struct LabelHalo {
    float tight;
    float wide;
    float vWide;
    vec3 chroma;  // R, G, B channels of the aberrated halo
};

LabelHalo gatherLabelHalo(vec2 uv, vec2 px, float labelGlowSpread, vec2 chromOff) {
    float haloTight = 0.0;
    float haloWide = 0.0;
    float haloVWide = 0.0;
    float haloR = 0.0, haloG = 0.0, haloB = 0.0;

    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float r2 = float(dx * dx + dy * dy);
            vec2 off = vec2(float(dx), float(dy)) * px;

            float wTight = exp(-r2 * 0.6);
            float wWide = exp(-r2 * 0.2);
            float wVWide = exp(-r2 * 0.1);

            float s = texture(uZoneLabels, uv + off * labelGlowSpread).a;
            haloTight += s * wTight;
            haloWide += s * wWide;
            haloVWide += s * wVWide;

            haloR += texture(uZoneLabels, uv + off * labelGlowSpread + chromOff).a * wWide;
            haloG += texture(uZoneLabels, uv + off * labelGlowSpread).a * wWide;
            haloB += texture(uZoneLabels, uv + off * labelGlowSpread - chromOff).a * wWide;
        }
    }

    LabelHalo h;
    h.tight = haloTight / 10.0;
    h.wide = haloWide / 16.5;
    h.vWide = haloVWide / 20.0;
    h.chroma = vec3(haloR, haloG, haloB) / 16.5;
    return h;
}

#endif // PLASMAZONES_LOGO_DRIFT_GLSL
