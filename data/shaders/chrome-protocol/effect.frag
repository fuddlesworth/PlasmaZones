// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

// CHROME PROTOCOL — 3D Stacked Targeting Array
// Faithful port of the Shadertoy 7-ring raymarched operator console,
// wrapped in per-zone rendering with full audio reactivity. The 7 ring
// SDFs are preserved verbatim from the reference; on top of that the
// thickness cycle, camera orbit, and overlay panels are driven by real
// audio bands (uAudioSpectrum) instead of mod-timers.

// ── SDF / rotation primitives ─────────────────────────────────────
float sdBox(vec2 p, vec2 s) { return max(abs(p.x) - s.x, abs(p.y) - s.y); }
float sdTri(vec2 p, vec2 s, float a) {
    return max(-dot(p, vec2(cos(-a), sin(-a))),
           max( dot(p, vec2(cos( a), sin( a))), sdBox(p, s)));
}
mat2 Rot(float a)   { return mat2(cos(a), -sin(a), sin(a), cos(a)); }
mat2 SkewX(float a) { return mat2(1.0, tan(a), 0.0, 1.0); }

// Domain folding — polar-symmetry helper from original (DF macro).
vec2 DF(vec2 a, float b) {
    float segs = b * 8.0;
    float lookup = mod(atan(a.y, a.x) + TAU / segs, TAU / (segs * 0.5))
                 + (b - 1.0) * TAU / segs;
    return length(a) * vec2(cos(lookup), cos(lookup + 11.0));
}

float cubicInOut(float t) {
    return t < 0.5 ? 4.0 * t * t * t : 0.5 * pow(2.0 * t - 2.0, 3.0) + 1.0;
}
float getTime(float t, float duration) { return clamp(t, 0.0, duration) / duration; }

// ── 7-segment digits ──────────────────────────────────────────────
float segBase(vec2 p) {
    vec2 g = mod(p, 0.05) - 0.025;
    float grid = min(abs(g.x) - 0.005, abs(g.y) - 0.005);
    float d = sdBox(p, vec2(0.075, 0.125));
    vec2 cp = vec2(abs(p.x) - 0.1, abs(p.y) - 0.05);
    d = max(dot(cp, vec2(0.7071, 0.7071)), d);
    return max(-grid, d);
}
float seg0(vec2 p) { return max(-sdBox(p, vec2(0.03, 0.081)), segBase(p)); }
float seg1(vec2 p) {
    float d = segBase(p);
    d = max(-sdBox(p + vec2(0.03, 0.03), vec2(0.06, 0.111)), d);
    return max(-sdBox(p + vec2(0.054, -0.105), vec2(0.03)), d);
}
float seg2(vec2 p) {
    float d = segBase(p);
    d = max(-sdBox(p + vec2(0.03, -0.05), vec2(0.06, 0.03)), d);
    return max(-sdBox(p - vec2(0.03, -0.05), vec2(0.06, 0.03)), d);
}
float seg3(vec2 p) {
    float d = segBase(p);
    vec2 q = vec2(p.x, abs(p.y));
    d = max(-sdBox(q + vec2(0.03, -0.05), vec2(0.06, 0.03)), d);
    return max(-sdBox(p + vec2(0.05, 0.0), vec2(0.03)), d);
}
float seg4(vec2 p) {
    float d = segBase(p);
    d = max(-sdBox(p + vec2(0.03, 0.08), vec2(0.06, 0.06)), d);
    return max(-sdBox(p + vec2(0.0, -0.08), vec2(0.03, 0.06)), d);
}
float seg5(vec2 p) {
    float d = segBase(p);
    d = max(-sdBox(p - vec2(0.03, 0.05), vec2(0.06, 0.03)), d);
    return max(-sdBox(p + vec2(0.03, 0.05), vec2(0.06, 0.03)), d);
}
float seg6(vec2 p) {
    float d = segBase(p);
    d = max(-sdBox(p - vec2(0.03, 0.05), vec2(0.06, 0.03)), d);
    return max(-sdBox(p - vec2(0.0, -0.05), vec2(0.03)), d);
}
float seg7(vec2 p) { return max(-sdBox(p + vec2(0.03, 0.03), vec2(0.06, 0.111)), segBase(p)); }
float seg8(vec2 p) {
    float d = segBase(p);
    vec2 q = vec2(p.x, abs(p.y));
    return max(-sdBox(q - vec2(0.0, 0.05), vec2(0.03)), d);
}
float seg9(vec2 p) {
    float d = segBase(p);
    d = max(-sdBox(p - vec2(0.0, 0.05), vec2(0.03)), d);
    return max(-sdBox(p + vec2(0.03, -0.05), vec2(0.06, 0.03)), d);
}
float segDP(vec2 p) { return max(sdBox(p + vec2(0.0, 0.1), vec2(0.028)), segBase(p)); }

float drawFont(vec2 p, int ch) {
    p *= 2.0;
    if (ch == 0) return seg0(p);
    if (ch == 1) return seg1(p);
    if (ch == 2) return seg2(p);
    if (ch == 3) return seg3(p);
    if (ch == 4) return seg4(p);
    if (ch == 5) return seg5(p);
    if (ch == 6) return seg6(p);
    if (ch == 7) return seg7(p);
    if (ch == 8) return seg8(p);
    if (ch == 9) return seg9(p);
    if (ch == 39) return segDP(p);
    return 10.0;
}

// ──
// 7 ring SDFs — PRESERVED VERBATIM from the original Shadertoy source.
// Each ring is a 2D slice; the raymarcher extrudes in Z (see GetDist).
// ──

float ring0(vec2 p) {
    vec2 prevP = p;
    p *= Rot(radians(-iTime * 30.0 + 50.0));
    p = DF(p, 16.0);
    p -= vec2(0.35);
    float d = sdBox(p * Rot(radians(45.0)), vec2(0.005, 0.03));
    p = prevP;
    p *= Rot(radians(-iTime * 30.0 + 50.0));
    float deg = 165.0;
    d = max(dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d);
    d = max(dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d);

    p = prevP;
    p *= Rot(radians(iTime * 30.0 + 30.0));
    float d2 = abs(length(p) - 0.55) - 0.015;
    d2 = max(-(abs(p.x) - 0.4), d2);
    d = min(d, d2);
    p = prevP;
    d = min(d, abs(length(p) - 0.55) - 0.001);

    p = prevP;
    p *= Rot(radians(-iTime * 50.0 + 30.0));
    p += sin(p * 25.0 - radians(iTime * 80.0)) * 0.01;
    d = min(d, abs(length(p) - 0.65) - 0.0001);

    p = prevP;
    float a = radians(-sin(iTime * 1.2)) * 120.0 + radians(-70.0);
    p.x += cos(a) * 0.58;
    p.y += sin(a) * 0.58;
    d = min(d, abs(sdTri(p * Rot(-a) * Rot(radians(90.0)), vec2(0.03), radians(45.0))) - 0.003);

    p = prevP;
    a = radians(sin(iTime * 1.3)) * 100.0 + radians(-10.0);
    p.x += cos(a) * 0.58;
    p.y += sin(a) * 0.58;
    d = min(d, abs(sdTri(p * Rot(-a) * Rot(radians(90.0)), vec2(0.03), radians(45.0))) - 0.003);
    return d;
}

float ring1(vec2 p) {
    vec2 prevP = p;
    float size = 0.45, deg = 140.0, thick = 0.02;
    float d = abs(length(p) - size) - thick;
    p *= Rot(radians(iTime * 60.0));
    d = max(dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d);
    d = max(dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d);
    return min(d, abs(length(prevP) - size) - 0.001);
}

float ring2(vec2 p) {
    float size = 0.3, deg = 120.0, thick = 0.02;
    p *= Rot(-radians(sin(iTime * 2.0) * 90.0));
    float d = abs(length(p) - size) - thick;
    d = max(dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d);
    d = max(dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d);
    float d2 = abs(length(p) - size) - thick;
    d2 = max(-dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d2);
    d2 = max(-dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d2);
    return min(d, d2);
}

float ring3(vec2 p) {
    p *= Rot(radians(-iTime * 80.0 - 120.0));
    vec2 prevP = p;
    float deg = 140.0;
    p = DF(p, 6.0);
    p -= vec2(0.3);
    float d = abs(sdBox(p * Rot(radians(45.0)), vec2(0.03, 0.025))) - 0.003;
    p = prevP;
    d = max(dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d);
    d = max(dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d);

    p = prevP;
    p = DF(p, 6.0);
    p -= vec2(0.3);
    float d2 = abs(sdBox(p * Rot(radians(45.0)), vec2(0.03, 0.025))) - 0.003;
    p = prevP;
    d2 = max(-dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d2);
    d2 = max(-dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d2);
    return min(d, d2);
}

float ring4(vec2 p) {
    p *= Rot(radians(iTime * 75.0 - 220.0));
    float deg = 20.0;
    float d = abs(length(p) - 0.25) - 0.01;
    p = DF(p, 2.0);
    p -= vec2(0.1);
    d = max(-dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d);
    d = max(-dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d);
    return d;
}

float ring5(vec2 p) {
    p *= Rot(radians(-iTime * 70.0 + 170.0));
    vec2 prevP = p;
    float deg = 150.0;
    float d = abs(length(p) - 0.16) - 0.02;
    d = max(dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d);
    d = max(dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d);
    p = prevP;
    p *= Rot(radians(-30.0));
    float d2 = abs(length(p) - 0.136) - 0.02;
    deg = 60.0;
    d2 = max(-dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d2);
    d2 = max(-dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d2);
    return min(d, d2);
}

float ring6(vec2 p) {
    vec2 prevP = p;
    p *= Rot(radians(iTime * 72.0 + 110.0));
    float d = abs(length(p) - 0.95) - 0.001;
    d = max(-(abs(p.x) - 0.4), d);
    d = max(-(abs(p.y) - 0.4), d);

    p = prevP;
    p *= Rot(radians(-iTime * 30.0 + 50.0));
    p = DF(p, 16.0);
    p -= vec2(0.6);
    float d2 = sdBox(p * Rot(radians(45.0)), vec2(0.02, 0.03));
    p = prevP;
    p *= Rot(radians(-iTime * 30.0 + 50.0));
    float deg = 155.0;
    d2 = max(-dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d2);
    d2 = max(-dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d2);
    return min(d, d2);
}

// ──
// 3D composition — 7 rings stacked along Z, audio drives thickness
// ──

float GetDist(vec3 p, float thickness) {
    p.z += 0.7;
    float d  = max(abs(p.z) - thickness, ring0(p.xy)); p.z -= 0.2;
    float d2 = max(abs(p.z) - thickness, ring1(p.xy)); d = min(d, d2); p.z -= 0.2;
    d2       = max(abs(p.z) - thickness, ring2(p.xy)); d = min(d, d2); p.z -= 0.2;
    d2       = max(abs(p.z) - thickness, ring3(p.xy)); d = min(d, d2); p.z -= 0.2;
    d2       = max(abs(p.z) - thickness, ring4(p.xy)); d = min(d, d2); p.z -= 0.2;
    d2       = max(abs(p.z) - thickness, ring5(p.xy)); d = min(d, d2); p.z -= 0.2;
    d2       = max(abs(p.z) - thickness, ring6(p.xy)); return min(d, d2);
}

// Alpha-accumulating raymarch: accumulates glow proportional to how
// close each step comes to the ring surface (from original RayMarch).
vec3 RayMarchT(vec3 ro, vec3 rd, int stepCount, float glowVal, float thickness) {
    float alpha = 0.0;
    float t = 0.0;
    const float tmax = 5.0;
    for (int i = 0; i < stepCount; i++) {
        vec3 pos = ro + rd * t;
        float d = GetDist(pos, thickness);
        if (t > tmax) break;
        alpha += 1.0 - smoothstep(0.0, glowVal, d);
        t += max(0.0001, abs(d) * 0.6);
    }
    alpha /= float(stepCount);
    return vec3(alpha * 1.5);
}

// Look-at camera ray (from original R helper).
vec3 CameraRay(vec2 uv, vec3 ro, vec3 la, float zoom) {
    vec3 f = normalize(la - ro);
    vec3 r = normalize(cross(vec3(0, 1, 0), f));
    vec3 u = cross(f, r);
    vec3 c = ro + f * zoom;
    vec3 i = c + uv.x * r + uv.y * u;
    return normalize(i - ro);
}

// ──
// Background hex grid (preserved from original bg(), thinned slightly
// so it reads as background texture instead of competing with rings)
// ──
float bg(vec2 p) {
    p.y -= iTime * 0.1;
    vec2 prevP = p;
    p *= 2.8;
    vec2 gv  = fract(p) - 0.5;
    vec2 gv2 = fract(p * 3.0) - 0.5;
    vec2 id  = floor(p);

    float d = min(sdBox(gv2, vec2(0.02, 0.09)), sdBox(gv2, vec2(0.09, 0.02)));
    float n = hash21(id);
    gv += vec2(0.166, 0.17);
    float d2 = abs(sdBox(gv, vec2(0.169))) - 0.004;

    if (n < 0.3) {
        gv *= Rot(radians(iTime * 60.0));
        d2 = max(-(abs(gv.x) - 0.08), d2);
        d2 = max(-(abs(gv.y) - 0.08), d2);
        d = min(d, d2);
    } else if (n < 0.6) {
        gv *= Rot(radians(-iTime * 60.0));
        d2 = max(-(abs(gv.x) - 0.08), d2);
        d2 = max(-(abs(gv.y) - 0.08), d2);
        d = min(d, d2);
    } else {
        gv *= Rot(radians(iTime * 60.0) + n);
        d2 = abs(length(gv) - 0.1) - 0.025;
        d2 = max(-(abs(gv.x) - 0.03), d2);
        d = min(d, abs(d2) - 0.003);
    }
    p = prevP;
    p = mod(p, 0.02) - 0.01;
    return min(d, sdBox(p, vec2(0.001)));
}

// ──
// Overlay UI — same shapes as original overlayUI(), with the
// rolling-block bars replaced by a real audio spectrum strip and the
// numeric readout showing live audio level.
// ──

// Number-with-circle: 4-digit "XX.YY" live audio readout with a
// rotating fragmented-arc border (DF polar-fold from the original).
float numberWithCircleUI(vec2 p, float audioVal) {
    vec2 prevP = p;
    p *= SkewX(radians(-15.0));
    float v = clamp(audioVal, 0.0, 0.999);
    int hi = int(v * 99.0);
    int lo = int(fract(v * 99.0) * 100.0);
    int d0 = hi / 10, d1 = hi - d0 * 10;
    int d2 = lo / 10, d3 = lo - d2 * 10;

    float d = drawFont(p - vec2(-0.16, 0.0), d0);
    d = min(d, drawFont(p - vec2(-0.08, 0.0), d1));
    d = min(d, drawFont(p - vec2(-0.02, 0.0), 39)); // DP

    vec2 q = p * 1.5;
    d = min(d, drawFont(q - vec2(0.04, -0.03), d2));
    d = min(d, drawFont(q - vec2(0.12, -0.03), d3));
    d = abs(d) - 0.002;

    p = prevP;
    p.x -= 0.07;
    p *= Rot(radians(-iTime * 50.0));
    p = DF(p, 4.0);
    p -= vec2(0.085);
    float d2b = sdBox(p * Rot(radians(45.0)), vec2(0.015, 0.018));
    p = prevP;
    d2b = max(-sdBox(p, vec2(0.13, 0.07)), d2b);
    return min(d, abs(d2b) - 0.0005);
}

// Classic rolling-block bars (preserved from original blockUI).
float blockUI(vec2 p) {
    vec2 prevP = p;
    p.x += iTime * 0.05;
    p.y = abs(p.y) - 0.02;
    p.x = mod(p.x, 0.04) - 0.02;
    float d = sdBox(p, vec2(0.0085));
    p = prevP;
    p.x += iTime * 0.05 + 0.02;
    p.x = mod(p.x, 0.04) - 0.02;
    d = min(d, sdBox(p, vec2(0.0085)));
    p = prevP;
    d = max(abs(p.x) - 0.2, d);
    return abs(d) - 0.0002;
}

// Audio spectrum bars — same footprint as blockUI but reads live audio.
// Returns a proper SDF: negative inside lit bars, positive outside.
float spectrumBarsUI(vec2 p, float audioReact) {
    if (iAudioSpectrumSize <= 0) return 10.0;
    float halfW = 0.2, halfH = 0.022;
    // Outside strip → far positive (no contribution)
    float stripDist = max(abs(p.x) - halfW, abs(p.y) - halfH);
    if (stripDist > 0.005) return 10.0;

    int n = min(iAudioSpectrumSize, 48);
    float cellW = 2.0 * halfW / float(n);
    float idxF = (p.x + halfW) / cellW;
    float idx = clamp(floor(idxF), 0.0, float(n - 1));
    float u = clamp(idx / float(max(n - 1, 1)), 0.0, 1.0);
    float v = clamp(audioBarSmooth(u) * audioReact, 0.02, 1.0);

    // Bar centered within its cell, grows symmetrically from y=0
    float cellLocalX = (idxF - idx - 0.5) * cellW;
    float barD = max(abs(cellLocalX) - cellW * 0.35, abs(p.y) - v * halfH);
    return barD;
}

// Two nested small tick circles (preserved from original smallCircleUI).
float smallCircleUI(vec2 p) {
    p *= 1.1;
    vec2 prevP = p;
    p *= Rot(radians(sin(iTime * 3.0) * 50.0));
    float d = abs(length(p) - 0.1) - 0.003;
    p = DF(p, 0.75);
    p -= vec2(0.02);
    float deg = 20.0;
    d = max(-dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d);
    d = max(-dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d);
    p = prevP;
    p *= Rot(radians(-sin(iTime * 2.0) * 80.0));
    float d2 = abs(length(p) - 0.08) - 0.001;
    d2 = max(-p.x, d2);
    d = min(d, d2);
    p = prevP;
    p *= Rot(radians(-iTime * 50.0));
    d2 = abs(length(p) - 0.05) - 0.015;
    deg = 170.0;
    d2 = max(-dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d2);
    d2 = max(-dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d2);
    return min(d, abs(d2) - 0.0005);
}

// Concentric tick donut (smallCircleUI2).
float smallCircleUI2(vec2 p) {
    float d = abs(length(p) - 0.04) - 0.0001;
    float d2 = length(p) - 0.03;
    p *= Rot(radians(iTime * 30.0));
    float deg = 140.0;
    d2 = max(-dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d2);
    d2 = max(-dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d2);
    d = min(d, d2);
    d2 = length(p) - 0.03;
    d2 = max(dot(p, vec2(cos(radians( deg)), sin(radians( deg)))), d2);
    d2 = max(dot(p, vec2(cos(radians(-deg)), sin(radians(-deg)))), d2);
    d = min(d, d2);
    return max(-(length(p) - 0.02), d);
}

// Rotating double-square with triangle accents (rectUI).
float rectUI(vec2 p) {
    p *= Rot(radians(45.0));
    vec2 prevP = p;
    float d = abs(sdBox(p, vec2(0.12))) - 0.003;
    p *= Rot(radians(iTime * 60.0));
    d = max(-(abs(p.x) - 0.05), d);
    d = max(-(abs(p.y) - 0.05), d);
    p = prevP;
    d = min(d, abs(sdBox(p, vec2(0.12))) - 0.0005);

    float d2 = abs(sdBox(p, vec2(0.09))) - 0.003;
    p *= Rot(radians(-iTime * 50.0));
    d2 = max(-(abs(p.x) - 0.03), d2);
    d2 = max(-(abs(p.y) - 0.03), d2);
    d = min(d, d2);
    p = prevP;
    d = min(d, abs(sdBox(p, vec2(0.09))) - 0.0005);

    p *= Rot(radians(-45.0));
    p.y = abs(p.y) - 0.07 - sin(iTime * 3.0) * 0.01;
    d = min(d, sdTri(p, vec2(0.02), radians(45.0)));
    p = prevP;
    p *= Rot(radians(45.0));
    p.y = abs(p.y) - 0.07 - sin(iTime * 3.0) * 0.01;
    d = min(d, sdTri(p, vec2(0.02), radians(45.0)));
    p = prevP;
    p *= Rot(radians(45.0));
    d2 = abs(sdBox(p, vec2(0.025))) - 0.0005;
    d2 = max(-(abs(p.x) - 0.01), d2);
    d2 = max(-(abs(p.y) - 0.01), d2);
    return min(d, d2);
}

// Horizontal bar rows — graphUI. Phase evolves at constant speed so the
// bars don't jitter when audio changes; audio contributes only to bar
// width (amplitude), not to the sin() argument.
float graphUI(vec2 p, float audioReact) {
    vec2 prevP = p;
    p.x += 0.5;
    p.y -= iTime * 0.25;
    p *= vec2(1.0, 100.0);
    vec2 gv = fract(p) - 0.5;
    vec2 id = floor(p);

    float u = fract(id.y * 0.13);
    float audio = audioBarSmooth(clamp(u, 0.0, 0.98)) * audioReact;
    float n = hash21(vec2(id.y)) * 2.0;
    // Phase driven by constant time only — audio scales amplitude additively
    float base = abs(sin(iTime * n) + 0.25) * 0.03 * n * 0.5;
    float w = base * (1.0 + audio * 1.5) + audio * 0.035;
    float d = sdBox(gv, vec2(w, 0.1));

    p = prevP;
    d = max(abs(p.x) - 0.2, d);
    return max(abs(p.y) - 0.2, d);
}

// Side tick marker (staticUI).
float staticUI(vec2 p) {
    vec2 prevP = p;
    float d = sdBox(p, vec2(0.005, 0.13));
    vec2 q = (p - vec2(0.02, -0.147)) * Rot(radians(-45.0));
    d = min(d, sdBox(q, vec2(0.005, 0.028)));
    d = min(d, sdBox(p - vec2(0.04, -0.2135), vec2(0.005, 0.049)));
    q = (p - vec2(0.02, -0.28)) * Rot(radians(45.0));
    d = min(d, sdBox(q, vec2(0.005, 0.03)));
    d = min(d, length(p - vec2(0.0,  0.13)) - 0.012);
    return min(d, length(p - vec2(0.0, -0.30)) - 0.012);
}

// Scrolling arrow strip. Constant-speed phase — audio must not
// modulate speed directly or frame-to-frame jumps cause jerky motion.
float arrowUI(vec2 p) {
    vec2 prevP = p;
    p.x *= -1.0;
    p.x -= iTime * 0.12;
    p.x = mod(p.x, 0.07) - 0.035 - 0.0325;
    p *= vec2(0.9, 1.5);
    p *= Rot(radians(90.0));
    float d = sdTri(p, vec2(0.05), radians(45.0));
    d = max(-sdTri(p - vec2(0.0, -0.03), vec2(0.05), radians(45.0)), d);
    d = abs(d) - 0.0005;
    return max(abs(prevP.x) - 0.15, d);
}

// Two stacked arrow bracket pair (sideUI / sideLine).
float sideLine(vec2 p) {
    p.x *= -1.0;
    vec2 prevP = p;
    p.y = abs(p.y) - 0.17;
    p *= Rot(radians(45.0));
    float d = sdBox(p, vec2(0.035, 0.01));
    p = prevP;
    d = min(d, sdBox(p - vec2(0.0217, 0.0), vec2(0.01, 0.152)));
    return abs(d) - 0.0005;
}
float sideUI(vec2 p) {
    vec2 prevP = p;
    p.x *= -1.0;
    p.x += 0.025;
    float d = sideLine(p);
    p = prevP;
    p.y = abs(p.y) - 0.275;
    return min(d, sideLine(p));
}

// Combined overlay layout (mirrors original overlayUI positions).
float overlayUI(vec2 p, float audioVal, float audioReact, bool useSpectrum) {
    vec2 prevP = p;
    float d = numberWithCircleUI(p - vec2(0.56, -0.34), audioVal);

    // Bottom / top block strips — spectrum bars if audio, else original blockUI
    p.x = abs(p.x) - 0.56;
    p.y -= 0.45;
    float bars = useSpectrum ? spectrumBarsUI(p, audioReact) : blockUI(p);
    d = min(d, bars);
    p = prevP;

    p.x = abs(p.x) - 0.72;
    p.y -= 0.35;
    d = min(d, smallCircleUI2(p));
    p = prevP;
    d = min(d, smallCircleUI2(p - vec2(-0.39, -0.42)));

    p = prevP;
    p.x -= 0.58;
    p.y -= 0.07;
    p.y = abs(p.y) - 0.12;
    d = min(d, smallCircleUI(p));

    p = prevP;
    d = min(d, rectUI(p - vec2(-0.58, -0.3)));

    vec2 gp = prevP - vec2(-0.58, 0.1);
    gp.x = abs(gp.x) - 0.05;
    d = min(d, graphUI(gp, audioReact));

    p = prevP;
    p.x = abs(p.x) - 0.72;
    p.y -= 0.13;
    d = min(d, staticUI(p));

    p = prevP;
    p.x = abs(p.x) - 0.51;
    p.y -= 0.35;
    d = min(d, arrowUI(p));

    p = prevP;
    p.x = abs(p.x) - 0.82;
    d = min(d, sideUI(p));
    return d;
}

// ──
// Global scene renderer — the whole raymarched HUD is drawn ONCE in
// screen-space and zones act as windows into the same continuous scene.
// Only border/glow/vitality remain per-zone (those belong to the zone
// chrome, not the underlying visualization).
// ──

struct GlobalParams {
    vec3 chromeCol;
    vec3 dataCol;
    vec3 scanCol;
    vec3 bgCol;
    float ringScale;
    float thickPulse;
    float audioReact;
    float bassBreath;
    float trebleGlitch;
    float surgeThresh;
    float hudBright;
    float spectrumOn;
    float showOverlay;
    float staticAmt;
    float scanDensity;
    float bgStrength;
    float mouseInfStr;
    int   stepCount;
    float aBass;
    float aMids;
    float aTreble;
    float aAll;
    bool  hasAudio;
};

vec3 renderGlobalScene(vec2 fragCoord, GlobalParams g) {
    // Screen-space uv — matches the original Shadertoy exactly:
    //   uv = (fragCoord - 0.5 * iResolution) / iResolution.y
    // Rings centre on the screen, not on individual zones, so multi-zone
    // layouts show a continuous HUD spanning across them.
    vec2 uv = (fragCoord - 0.5 * iResolution) / max(iResolution.y, 1.0)
            / max(g.ringScale, 0.1);
    vec2 screenUV = fragCoord / max(iResolution, vec2(1.0));

    // Treble-driven horizontal glitch displacement (screen-space bands)
    if (g.trebleGlitch > 0.01 && g.aTreble > 0.05) {
        float slot = floor(iTime * 22.0);
        if (hash11(slot) > 0.82) {
            float bandY = hash11(slot + 100.0);
            if (abs(screenUV.y - bandY) < 0.04 + hash11(slot + 200.0) * 0.05) {
                uv.x += (hash11(slot + 300.0) - 0.5) * g.trebleGlitch * g.aTreble * 0.12;
            }
        }
    }

    // Base: dark background
    vec3 col = g.bgCol;

    // Hex grid (screen-space)
    if (g.bgStrength > 0.001) {
        float bd = bg(uv);
        float bgMask = 1.0 - smoothstep(0.0, 2.0 / max(iResolution.y, 1.0), bd);
        vec3 gridCol = mix(g.chromeCol, g.scanCol, g.aBass * 0.3) * (0.55 + g.aMids * 0.2);
        col = mix(col, gridCol, bgMask * g.bgStrength);
    }

    // Thickness cycle — 30s alternation between thick (0.03) and thin (0.007)
    float frame = mod(iTime, 30.0);
    float thickness = 0.03;
    const float maxThick = 0.03;
    const float minThick = 0.007;
    if (frame >= 10.0 && frame < 20.0) {
        float tt = getTime(frame - 10.0, 1.5);
        thickness = (maxThick + minThick) - cubicInOut(tt) * maxThick;
    } else if (frame >= 20.0) {
        float tt = getTime(frame - 20.0, 1.5);
        thickness = minThick + cubicInOut(tt) * maxThick;
    }
    thickness = mix(thickness, maxThick * 1.2, g.aBass * g.thickPulse * 0.6);

    // Camera orbit — same cycle as original
    float camCycleYZ = 45.0;
    float camOgRXZ = 50.0;
    float camAnimRXZ = 20.0;
    if (frame >= 10.0 && frame < 20.0) {
        float tt = getTime(frame - 10.0, 1.5);
        camCycleYZ *= 1.0 - cubicInOut(tt);
        camOgRXZ   *= 1.0 - cubicInOut(tt);
        camAnimRXZ *= 1.0 - cubicInOut(tt);
    } else if (frame >= 20.0) {
        float tt = getTime(frame - 20.0, 1.5);
        camCycleYZ *= cubicInOut(tt);
        camOgRXZ   *= cubicInOut(tt);
        camAnimRXZ *= cubicInOut(tt);
    }
    camCycleYZ += g.aBass * g.bassBreath * 60.0;
    float orbit = sin(iTime * 0.3) * camAnimRXZ * (1.0 + g.aMids * 0.5) + camOgRXZ;

    vec3 ro = vec3(0.0, 0.0, -2.1 - g.aBass * g.bassBreath * 0.15);
    ro.yz *= Rot(radians(camCycleYZ));
    ro.xz *= Rot(radians(orbit));

    // Mouse: global tilt when cursor is on the layout (no per-zone gating)
    if (g.mouseInfStr > 0.01) {
        vec2 m = (screenUV - 0.5) * 2.0;
        ro.yz *= Rot(-m.y * 3.14 * 0.1 * g.mouseInfStr);
        ro.xz *= Rot(-m.x * 6.28 * 0.06 * g.mouseInfStr);
    }

    vec3 rd = CameraRay(uv, ro, vec3(0.0), 1.0);
    vec3 d3d = RayMarchT(ro, rd, g.stepCount, 0.003, thickness);

    float surge = step(g.surgeThresh, g.aBass) * (g.aBass - g.surgeThresh)
                / max(1.0 - g.surgeThresh, 0.01);
    surge = clamp(surge * surge, 0.0, 1.0);
    float glowMul = 1.0 + g.aBass * 0.6 + surge * 1.5;

    vec3 ringTint = mix(g.dataCol, g.chromeCol, clamp(length(uv) * 1.5, 0.0, 1.0));
    ringTint = mix(ringTint, g.scanCol, surge * 0.75);
    col = mix(col, d3d * ringTint * glowMul, 0.7);

    // Chromatic aberration (screen-space, uses iResolution.y)
    if (g.aBass > 0.05 || surge > 0.01) {
        float chromaStr = (0.4 + g.aBass * 2.0 + surge * 2.5) / max(iResolution.y, 1.0);
        vec2 chromaOff = vec2(chromaStr, 0.0);
        vec3 rdR = CameraRay(uv + chromaOff, ro, vec3(0.0), 1.0);
        vec3 rdB = CameraRay(uv - chromaOff, ro, vec3(0.0), 1.0);
        float aR = RayMarchT(ro, rdR, max(g.stepCount - 16, 16), 0.003, thickness).r;
        float aB = RayMarchT(ro, rdB, max(g.stepCount - 16, 16), 0.003, thickness).r;
        col.r += aR * g.chromeCol.r * 0.25 * glowMul;
        col.b += aB * g.dataCol.b   * 0.25 * glowMul;
    }

    // Overlay UI moved to per-zone pass (see renderZoneChrome) so it
    // stays visible inside zone bounds. Rings + bg span the screen here.

    // Static noise + scanlines (screen-space)
    if (g.staticAmt > 0.0001) {
        float sn = hash21(fragCoord + iTime * 100.0) * g.staticAmt;
        col += g.chromeCol * sn * 0.25 * (0.5 + g.aTreble);
    }
    if (g.scanDensity > 0.001) {
        col *= 0.94 + 0.06 * sin(fragCoord.y * 1.5 * g.scanDensity);
    }

    // Gamma (matches original col = pow(col, 0.9545))
    return pow(max(col, 0.0), vec3(0.9545));
}

// Per-zone chrome: fill (from global scene with vitality), border, glow.
vec4 renderZoneChrome(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                      vec4 zParams, vec3 sceneCol, GlobalParams g, bool isHighlighted) {
    float borderRadius = max(zParams.x, 6.0);
    float borderWidth  = max(zParams.y, 2.5);
    float fillOpacity  = customParams[4].y;
    float edgeGlow     = customParams[3].z;

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    float px      = pxScale();

    float vitality = zoneVitality(isHighlighted);
    vec3 zChromeCol = vitalityDesaturate(g.chromeCol, vitality);
    vec3 zDataCol   = vitalityDesaturate(g.dataCol, vitality);
    float highlightBoost = vitalityScale(0.75, 1.35, vitality);
    fillOpacity = mix(fillOpacity, min(fillOpacity + 0.1, 0.95), vitality);

    vec4 result = vec4(0.0);
    if (d < 0.0) {
        // Zone shows the global scene, desaturated when dormant, with a
        // slight fillColor tint layered underneath for zone identity.
        vec3 col = vitalityDesaturate(sceneCol, vitality);
        col = mix(col, col * 0.85 + fillColor.rgb * 0.15, 0.35);

        // Overlay HUD — per-zone in zone-local aspect-corrected UV so the
        // 7-seg readout, spectrum bars, and edge panels land inside the
        // zone that's showing the effect. Rings/bg still span globally.
        if (g.showOverlay > 0.5) {
            vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
            float zAspect = rectSize.x / max(rectSize.y, 1.0);
            vec2 ouv = (localUV - 0.5) * vec2(zAspect, 1.0);
            bool useSpec = g.spectrumOn > 0.5 && g.hasAudio;
            float od = overlayUI(ouv, g.aAll / max(g.audioReact, 0.01),
                                 g.audioReact, useSpec);
            float om = 1.0 - smoothstep(0.0, 2.5 / max(rectSize.y, 1.0), od);
            vec3 overlayTint = mix(zChromeCol, g.scanCol, 0.3 + g.aBass * 0.3);
            col = mix(col, overlayTint * 1.3 * g.hudBright * highlightBoost, om);
        }

        col *= highlightBoost;
        result = vec4(col, fillOpacity * fillColor.a);
    }

    // Border
    float borderFactor = softBorder(d, borderWidth);
    if (borderFactor > 0.001) {
        vec3 borderTint = mix(zChromeCol, zDataCol, 0.3 + g.aBass * 0.25);
        vec3 borderFinal = mix(borderTint, borderColor.rgb, 0.3);
        borderFinal *= (1.0 + g.aBass * 0.4) * g.hudBright * highlightBoost;
        result = blendOver(result, vec4(borderFinal * borderFactor, borderFactor * borderColor.a));
    }

    // Outer glow
    if (d > 0.0 && d < 50.0 * px) {
        float rim = exp(-d / ((8.0 + g.aBass * 10.0) * px)) * edgeGlow * vitality;
        vec3 rimCol = mix(zChromeCol, zDataCol, g.aBass * 0.35);
        result = blendOver(result, vec4(rimCol * rim * highlightBoost, rim * 0.5));
    }
    return result;
}

// ──
// Main — render the scene once in screen-space, then loop zones for chrome
// ──

void main() {
    bool hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    float overall = getOverallSoft();

    GlobalParams g;
    g.chromeCol    = colorWithFallback(customColors[0].rgb, vec3(0.776, 0.831, 0.890));
    g.dataCol      = colorWithFallback(customColors[1].rgb, vec3(0.129, 0.784, 1.000));
    g.scanCol      = colorWithFallback(customColors[2].rgb, vec3(0.918, 0.965, 1.000));
    g.bgCol        = colorWithFallback(customColors[3].rgb, vec3(0.027, 0.043, 0.071));
    g.ringScale    = customParams[0].x;
    g.thickPulse   = customParams[0].w;
    g.audioReact   = customParams[1].x;
    g.bassBreath   = customParams[1].y;
    g.trebleGlitch = customParams[1].z;
    g.surgeThresh  = customParams[1].w;
    g.hudBright    = customParams[2].x;
    g.spectrumOn   = customParams[2].y;
    g.showOverlay  = customParams[2].z;
    g.staticAmt    = customParams[3].x;
    g.scanDensity  = customParams[3].y;
    g.bgStrength   = customParams[3].w;
    g.mouseInfStr  = customParams[4].x;
    g.stepCount    = int(max(customParams[4].z, 16.0));
    g.hasAudio     = hasAudio;
    g.aBass   = hasAudio ? bass    * g.audioReact : 0.0;
    g.aMids   = hasAudio ? mids    * g.audioReact : 0.0;
    g.aTreble = hasAudio ? treble  * g.audioReact : 0.0;
    g.aAll    = hasAudio ? overall * g.audioReact : 0.0;

    // Render the HUD scene once in screen-space — shared across all zones
    vec3 sceneCol = renderGlobalScene(vFragCoord, g);

    // Per-zone: gate visibility, apply vitality, draw border + glow
    vec4 result = vec4(0.0);
    for (int i = 0; i < zoneCount && i < 64; i++) {
        result = blendOver(result, renderZoneChrome(
            vFragCoord, zoneRects[i], zoneFillColors[i], zoneBorderColors[i],
            zoneParams[i], sceneCol, g, zoneParams[i].z > 0.5));
    }

    // Labels: HUD designation plate. The body stays in [0,1] color space
    // so chrome/data tinting actually survives — labelBright scales glow
    // intensity, not body saturation (a labelBright of 2.4 applied to the
    // body pushed everything past the tonemap ceiling and bleached it white).
    bool showLabels = customParams[5].w > 0.5;
    if (showLabels) {
        float labelSpread = customParams[5].x;
        float labelBright = customParams[5].y;
        float labelReact  = customParams[5].z;

        vec3 lChromeCol = colorWithFallback(customColors[0].rgb, vec3(0.776, 0.831, 0.890));
        vec3 lDataCol   = colorWithFallback(customColors[1].rgb, vec3(0.129, 0.784, 1.000));
        vec3 lScanCol   = colorWithFallback(customColors[2].rgb, vec3(0.918, 0.965, 1.000));

        vec2 luv = labelsUv(vFragCoord);
        vec2 texPx = 1.0 / max(iResolution, vec2(1.0));
        vec4 labels = texture(uZoneLabels, luv);
        float spread = labelSpread * pxScale();
        float bassMod = hasAudio ? bass * labelReact : 0.0;

        // Tight Gaussian halo — 5x5 kernel, narrow falloff so glow sits
        // close to glyphs instead of smearing across ring detail
        float halo = 0.0;
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                float r2 = float(dx * dx + dy * dy);
                halo += texture(uZoneLabels, luv + vec2(float(dx), float(dy)) * texPx * spread).a
                      * exp(-r2 * 0.45);
            }
        }
        halo /= 10.5;

        float flicker = 0.96 + 0.04 * sin(iTime * 7.3 + luv.x * 3.0);
        flicker *= (1.0 + bassMod * 0.25);

        // Halo: data-blue bias with chrome mid and scan-white core.
        // labelBright scales halo intensity (that's what the knob should do).
        if (halo > 0.002) {
            float haloEdge = halo * (1.0 - labels.a);
            float coreAmt = smoothstep(0.1, 0.45, haloEdge);
            vec3 haloCol = mix(lDataCol * 0.9, lChromeCol, 0.35);
            haloCol = mix(haloCol, lScanCol, coreAmt);
            result.rgb += haloCol * haloEdge * 0.55 * flicker * labelBright;
            result.a = max(result.a, haloEdge * 0.55);
        }

        if (labels.a > 0.01) {
            // Rim stencil — computed first so body can reference it
            float aL = texture(uZoneLabels, luv + vec2(-texPx.x, 0.0)).a;
            float aR = texture(uZoneLabels, luv + vec2( texPx.x, 0.0)).a;
            float aU = texture(uZoneLabels, luv + vec2(0.0, -texPx.y)).a;
            float aD = texture(uZoneLabels, luv + vec2(0.0,  texPx.y)).a;
            float rim = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.2, 0.0, 1.0);

            // Body: data-blue-heavy teal base (stays visibly colored, not white).
            // The chrome ups the lightness; mix factor keeps saturation alive.
            vec3 body = mix(lDataCol * 0.85, lChromeCol * 0.8, 0.35);
            // Rim sharpens to bright scan-white for legibility
            vec3 textCol = mix(body, lScanCol * 0.95, rim * 0.7);
            // Bass pulse as a subtle multiplier (not an amplifier beyond 1)
            textCol *= flicker * (1.0 + bassMod * 0.2);

            // Match the ring gamma path so text lives in the same exposure
            textCol = pow(max(textCol, 0.0), vec3(0.9545));

            result.rgb = mix(result.rgb, textCol, labels.a);
            result.a = max(result.a, labels.a);
        }
    }

    fragColor = clampFragColor(result);
}
