// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * ENDEAVOUROS DRIFT - Fragment Shader (Tri-Sail Community Constellation)
 *
 * Constellation network background: orbiting dots on Lissajous curves
 * connected by proximity lines, over a warm purple-to-coral gradient wash.
 * Three overlapping sail SDF shapes with per-sail coloring, simplex noise
 * interior flow, per-sail bass pulses, summit convergence glow, and
 * shared-edge highlights.
 *
 * Logo geometry: 3 overlapping sail polygons (26+16+16 vertices)
 * from the EndeavourOS logo.
 *
 * Audio reactivity:
 *   Bass  = constellation lines brighten, sail pulses, summit glow
 *   Mids  = interior noise churn, palette warmth drift
 *   Treble = dots twinkle, edge sparks, shared-edge flare
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// -- EndeavourOS brand constants ------------------------------------------
const vec3 EOS_PERI   = vec3(0.498, 0.498, 1.000); // Periwinkle (blue sail)
const vec3 EOS_PURPLE = vec3(0.498, 0.247, 0.749); // Purple sail
const vec3 EOS_CORAL  = vec3(1.000, 0.498, 0.498); // Coral sail (red)
const vec3 EOS_GLOW   = vec3(0.565, 0.506, 0.733); // Lavender glow

const vec2 LOGO_CENTER = vec2(0.583, 0.52);


// -- Simplex noise --------------------------------------------------------

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

// -- SDF primitives -------------------------------------------------------
float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// -- Catmull-Rom palette interpolation ------------------------------------
vec3 catmullRom(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5 * ((2.0 * p1) +
                   (-p0 + p2) * t +
                   (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                   (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

vec3 eosPaletteCR(float t, vec3 primary, vec3 secondary, vec3 accent, vec3 glow) {
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
    float shifted = t + audioShift * 0.08;
    return eosPaletteCR(shifted, primary, secondary, accent, glow);
}

// =========================================================================
//  ENDEAVOUROS LOGO -- 3 overlapping sail polygons
// =========================================================================
// Blue sail (#7f7fff) -- 60 vertices (12 samples per bezier curve)
const int EOS_BLUE_N = 60;
const vec2 EOS_BLUE_POLY[60] = vec2[60](
    vec2(0.584496, 0.094757),
    vec2(0.611989, 0.133082), vec2(0.648179, 0.183808), vec2(0.689898, 0.244353),
    vec2(0.733979, 0.312132), vec2(0.777254, 0.384561), vec2(0.816557, 0.459057),
    vec2(0.848718, 0.533036), vec2(0.870571, 0.603913), vec2(0.878949, 0.669106),
    vec2(0.870684, 0.726030), vec2(0.842608, 0.772101), vec2(0.791554, 0.804736),
    vec2(0.753718, 0.815578), vec2(0.701678, 0.821381), vec2(0.638801, 0.822986),
    vec2(0.568454, 0.821229), vec2(0.494006, 0.816952), vec2(0.418824, 0.810992),
    vec2(0.346274, 0.804189), vec2(0.279725, 0.797381), vec2(0.222545, 0.791409),
    vec2(0.178100, 0.787110), vec2(0.149758, 0.785324), vec2(0.140886, 0.786891),
    vec2(0.131409, 0.802511), vec2(0.122334, 0.817476), vec2(0.113742, 0.831652),
    vec2(0.105712, 0.844906), vec2(0.098325, 0.857103), vec2(0.091663, 0.868109),
    vec2(0.085804, 0.877790), vec2(0.080831, 0.886011), vec2(0.076822, 0.892639),
    vec2(0.073859, 0.897539), vec2(0.071392, 0.901620), vec2(0.079748, 0.901964),
    vec2(0.103641, 0.902808), vec2(0.141308, 0.903871), vec2(0.190987, 0.904872),
    vec2(0.250916, 0.905532), vec2(0.319334, 0.905568), vec2(0.394476, 0.904701),
    vec2(0.474583, 0.902649), vec2(0.557890, 0.899132), vec2(0.642637, 0.893869),
    vec2(0.727061, 0.886579), vec2(0.809399, 0.876982), vec2(0.903982, 0.851157),
    vec2(0.961333, 0.806811), vec2(0.986440, 0.747353), vec2(0.984292, 0.676193),
    vec2(0.959877, 0.596740), vec2(0.918182, 0.512403), vec2(0.864197, 0.426592),
    vec2(0.802909, 0.342717), vec2(0.739307, 0.264186), vec2(0.678378, 0.194410),
    vec2(0.625112, 0.136797), vec2(0.584496, 0.094757)
);

// Coral sail (#ff7f7f) -- 36 vertices
const int EOS_RED_N = 36;
const vec2 EOS_RED_POLY[36] = vec2[36](
    vec2(0.583492, 0.094432),
    vec2(0.566080, 0.108444), vec2(0.531030, 0.145373), vec2(0.481992, 0.200617),
    vec2(0.422618, 0.269578), vec2(0.356558, 0.347654), vec2(0.287461, 0.430246),
    vec2(0.218980, 0.512752), vec2(0.154763, 0.590573), vec2(0.098462, 0.659109),
    vec2(0.053728, 0.713758), vec2(0.024210, 0.749922), vec2(0.013560, 0.762998),
    vec2(0.021009, 0.764920), vec2(0.026903, 0.766330), vec2(0.034565, 0.768072),
    vec2(0.044034, 0.770114), vec2(0.055346, 0.772424), vec2(0.068540, 0.774971),
    vec2(0.083654, 0.777723), vec2(0.100724, 0.780648), vec2(0.119789, 0.783715),
    vec2(0.140886, 0.786891), vec2(0.155414, 0.768360), vec2(0.182869, 0.727096),
    vec2(0.220597, 0.667784), vec2(0.265943, 0.595109), vec2(0.316253, 0.513755),
    vec2(0.368874, 0.428407), vec2(0.421152, 0.343748), vec2(0.470432, 0.264463),
    vec2(0.514060, 0.195238), vec2(0.549383, 0.140755), vec2(0.573746, 0.105700),
    vec2(0.584496, 0.094757), vec2(0.583492, 0.094432)
);

// Purple sail (#7f3fbf) -- 37 vertices
const int EOS_PURP_N = 37;
const vec2 EOS_PURP_POLY[37] = vec2[37](
    vec2(0.584021, 0.094540),
    vec2(0.571397, 0.107519), vec2(0.544741, 0.144762), vec2(0.506938, 0.201427),
    vec2(0.460872, 0.272677), vec2(0.409425, 0.353670), vec2(0.355482, 0.439566),
    vec2(0.301927, 0.525527), vec2(0.251642, 0.606712), vec2(0.207512, 0.678280),
    vec2(0.172420, 0.735393), vec2(0.149250, 0.773210), vec2(0.140886, 0.786891),
    vec2(0.148561, 0.787691), vec2(0.170166, 0.789896), vec2(0.203574, 0.793213),
    vec2(0.246658, 0.797351), vec2(0.297291, 0.802017), vec2(0.353343, 0.806919),
    vec2(0.412689, 0.811764), vec2(0.473200, 0.816260), vec2(0.532749, 0.820116),
    vec2(0.589209, 0.823038), vec2(0.640452, 0.824734), vec2(0.684350, 0.824913),
    vec2(0.778346, 0.811569), vec2(0.839477, 0.777701), vec2(0.872022, 0.726874),
    vec2(0.880258, 0.662654), vec2(0.868465, 0.588609), vec2(0.840922, 0.508305),
    vec2(0.801906, 0.425307), vec2(0.755697, 0.343184), vec2(0.706572, 0.265500),
    vec2(0.658812, 0.195824), vec2(0.616693, 0.137721), vec2(0.584496, 0.094757)
);

const vec2 EOS_AABB_LO = vec2(0.013, 0.094);
const vec2 EOS_AABB_HI = vec2(0.987, 0.906);

// Per-sail tight AABBs (computed from vertex data + 0.005 padding)
const vec2 BLUE_AABB_LO = vec2(0.066, 0.090);
const vec2 BLUE_AABB_HI = vec2(0.991, 0.911);
const vec2 RED_AABB_LO  = vec2(0.009, 0.089);
const vec2 RED_AABB_HI  = vec2(0.589, 0.792);
const vec2 PURP_AABB_LO = vec2(0.136, 0.090);
const vec2 PURP_AABB_HI = vec2(0.885, 0.830);

// -- Polygon SDF core (winding-number approach) ---------------------------
// Shared loop body: returns vec2(signedDist, edgeDist).
// We specialize per array size to satisfy GLSL 450 type requirements.

#define POLY_SDF_BODY(POLY, N) \
    float d = dot(p - POLY[0], p - POLY[0]); \
    float s = 1.0; \
    for (int i = 0, j = N - 1; i < N; j = i, i++) { \
        vec2 e = POLY[j] - POLY[i]; \
        vec2 w = p - POLY[i]; \
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0); \
        d = min(d, dot(b, b)); \
        bvec3 cond = bvec3(p.y >= POLY[i].y, p.y < POLY[j].y, e.x * w.y > e.y * w.x); \
        if (all(cond) || all(not(cond))) s *= -1.0; \
    } \
    float edgeDist = sqrt(d);

// SDF with per-sail tight AABB early-out returning vec2(signedDist, edgeDist)
vec2 sdBlueWithEdge(vec2 p) {
    vec2 dLo = BLUE_AABB_LO - p; vec2 dHi = p - BLUE_AABB_HI;
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.0004) { float bd = sqrt(boxDist2); return vec2(bd, bd); }
    POLY_SDF_BODY(EOS_BLUE_POLY, EOS_BLUE_N)
    return vec2(s * edgeDist, edgeDist);
}
vec2 sdRedWithEdge(vec2 p) {
    vec2 dLo = RED_AABB_LO - p; vec2 dHi = p - RED_AABB_HI;
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.0004) { float bd = sqrt(boxDist2); return vec2(bd, bd); }
    POLY_SDF_BODY(EOS_RED_POLY, EOS_RED_N)
    return vec2(s * edgeDist, edgeDist);
}
vec2 sdPurpWithEdge(vec2 p) {
    vec2 dLo = PURP_AABB_LO - p; vec2 dHi = p - PURP_AABB_HI;
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.0004) { float bd = sqrt(boxDist2); return vec2(bd, bd); }
    POLY_SDF_BODY(EOS_PURP_POLY, EOS_PURP_N)
    return vec2(s * edgeDist, edgeDist);
}


// -- Sail hit result ------------------------------------------------------

struct SailHit {
    float dist;       // signed distance
    float edgeDist;   // always positive edge distance
    int sailIndex;    // 0=red(coral), 1=purple, 2=blue -- painter's order
    vec3 sailColor;   // fill color for this sail
    float unionDist;  // min signed distance across all 3 sails
    int overlapCount; // number of sails overlapping at this point (0-3)
};

// Evaluate all 3 sails at point p, return the topmost hit (painter's order)
// Per-sail tight AABBs in each sd*WithEdge skip the expensive polygon loop
// for pixels far from that sail's bounding box.
SailHit evalEosSails(vec2 p, vec3 colBlue, vec3 colCoral, vec3 colPurple) {
    // Painter's order: coral first (back), purple second, blue on top (front)
    vec2 redResult = sdRedWithEdge(p);
    vec2 purpResult = sdPurpWithEdge(p);
    vec2 blueResult = sdBlueWithEdge(p);

    SailHit hit;
    hit.unionDist = min(redResult.x, min(purpResult.x, blueResult.x));
    hit.dist = hit.unionDist;
    hit.edgeDist = min(redResult.y, min(purpResult.y, blueResult.y));
    hit.sailIndex = -1;
    hit.sailColor = vec3(0.0);

    // Branchless overlap count
    hit.overlapCount = int(step(0.0, -redResult.x) + step(0.0, -purpResult.x) + step(0.0, -blueResult.x));

    // Topmost layer wins (blue on top)
    if (redResult.x < 0.0) {
        hit.dist = redResult.x;
        hit.edgeDist = redResult.y;
        hit.sailIndex = 0;
        hit.sailColor = colCoral;
    }
    if (purpResult.x < 0.0) {
        hit.dist = purpResult.x;
        hit.edgeDist = purpResult.y;
        hit.sailIndex = 1;
        hit.sailColor = colPurple;
    }
    if (blueResult.x < 0.0) {
        hit.dist = blueResult.x;
        hit.edgeDist = blueResult.y;
        hit.sailIndex = 2;
        hit.sailColor = colBlue;
    }

    return hit;
}



// -- Per-instance UV computation ------------------------------------------
vec2 computeInstanceUV(int idx, int totalCount, vec2 globalUV, float aspect, float time,
                       float logoScale, float bassEnv, float logoPulse,
                       float sizeMin, float sizeMax, out float instScale) {
    vec2 uv = globalUV;
    float wobbleAmp = customParams[7].z >= 0.0 ? customParams[7].z : 0.12;
    uv.x = (uv.x - 0.5) * aspect + 0.5;
    if (totalCount <= 1) {
        uv -= vec2(sin(time*0.11)*0.015+sin(time*0.27)*0.005, cos(time*0.14)*0.008+cos(time*0.09)*0.004);
        float ra = sin(time * 0.08) * wobbleAmp;
        vec2 lp = uv - 0.5;
        uv = vec2(lp.x*cos(ra)-lp.y*sin(ra), lp.x*sin(ra)+lp.y*cos(ra)) + 0.5;
        instScale = logoScale * (1.0 + sin(time * 0.5) * 0.01);
        return (uv - 0.5) / instScale + LOGO_CENTER;
    }
    float h1=hash21(vec2(float(idx)*7.31,3.17)), h2=hash21(vec2(float(idx)*13.71,7.23));
    float h3=hash21(vec2(float(idx)*5.13,11.37)), h4=hash21(vec2(float(idx)*9.77,17.53));
    float f1=0.06+float(idx)*0.017, f2=0.04+float(idx)*0.013;
    uv -= vec2(sin(time*f1+h1*TAU)*0.3+sin(time*f1*2.1+h3*TAU)*0.075,
               cos(time*f2+h2*TAU)*0.255+cos(time*f2*1.5+h4*TAU)*0.06);
    float ra = sin(time*(0.07+float(idx)*0.019)+h4*TAU) * wobbleAmp * 0.33;
    vec2 lp = uv - 0.5;
    uv = vec2(lp.x*cos(ra)-lp.y*sin(ra), lp.x*sin(ra)+lp.y*cos(ra)) + 0.5;
    instScale = mix(sizeMin, sizeMax, h3) * logoScale;
    instScale *= 1.0 + sin(time*(0.4+float(idx)*0.09)+h1*TAU) * 0.012;
    return (uv - 0.5) / instScale + LOGO_CENTER;
}


// =========================================================================
//  CONSTELLATION NETWORK BACKGROUND
// =========================================================================

const int CONSTELLATION_COUNT = 14;

vec3 constellationNetwork(vec2 uv, float time, float bassEnv, float midsEnv, float trebleEnv,
                          float particleStr, float connThreshold,
                          float orbitScale, float lineStr, vec2 orbitCenter,
                          vec3 palPrimary, vec3 palSecondary, vec3 palAccent, vec3 palGlow) {
    vec3 col = vec3(0.0);
    vec2 dots[14]; vec3 dotColors[14];
    // Bass makes dots scatter outward from center (explosive feel)
    float scatter = 1.0 + bassEnv * 0.6;
    for (int i = 0; i < CONSTELLATION_COUNT; i++) {
        float fi = float(i);
        float h1=hash21(vec2(fi*7.13,3.91)), h2=hash21(vec2(fi*11.37,17.53)), h3=hash21(vec2(fi*5.79,23.11));
        dots[i] = vec2(orbitCenter.x + sin(time*(0.12+h1*0.18)+h1*TAU)*(0.32+h3*0.15)*(orbitScale)*scatter,
                        orbitCenter.y + cos(time*(0.10+h2*0.16)+h2*TAU)*(0.28+h1*0.12)*(orbitScale)*scatter);
        float cs = mod(fi, 3.0);
        dotColors[i] = cs < 1.0 ? palPrimary : cs < 2.0 ? palSecondary : palAccent;
    }

    // Mids expand the connection radius — more connections on mid-heavy music
    float connectionThreshold = connThreshold * 0.6 + midsEnv * 0.2;

    for (int i = 0; i < CONSTELLATION_COUNT; i++) {
        vec2 dPos = dots[i];
        float dist = length(uv - dPos);
        float fi = float(i);

        // Dot glow: treble makes dots twinkle
        float twinklePhase = hash21(vec2(fi, floor(time * 4.0)));
        float dotBright = 1.0 + bassEnv * 0.5 + trebleEnv * 2.0 * step(0.4, twinklePhase);
        float dotRadius = 0.007 + bassEnv * 0.003;
        float dotMask = smoothstep(dotRadius, dotRadius * 0.1, dist);
        float dotHalo = exp(-dist * 50.0) * (0.25 + bassEnv * 0.25);

        col += dotColors[i] * (dotMask + dotHalo) * dotBright * particleStr;

        // Treble flash (merged here to reuse dist — avoids second 14-iteration loop)
        float flashChance = step(0.5, hash21(vec2(fi, floor(time * 6.0))));
        col += dotColors[i] * exp(-dist * 30.0) * trebleEnv * 2.0 * flashChance;

        // Skip inner loop if fragment is far from this dot — no connection
        // line can be visible if we're further than the max connection range
        if (dist > connectionThreshold + 0.04) continue;

        // Connection lines to nearby dots
        for (int j = i + 1; j < CONSTELLATION_COUNT; j++) {
            vec2 dPos2 = dots[j];
            float dotDist = length(dPos - dPos2);

            if (dotDist < connectionThreshold) {
                float lineDist = sdSegment(uv, dPos, dPos2);
                float lineWidth = 0.003;
                float lineMask = smoothstep(lineWidth, lineWidth * 0.1, lineDist);

                float proximity = 1.0 - dotDist / connectionThreshold;
                proximity *= proximity;

                float lineBass = 0.6 + bassEnv * 1.5;
                float linePhase = fract(time * 0.25 + hash21(vec2(fi, float(j))) * TAU);
                float linePulse = 0.5 + 0.5 * sin(linePhase * TAU);

                vec3 lineCol = mix(dotColors[i], dotColors[j], 0.5);
                col += lineCol * 0.5 * lineMask * proximity * linePulse * lineBass * particleStr * lineStr;
            }
        }
    }

    // ── Bass shockwave: expanding ring from center on bass hits ──
    {
        // 2 staggered shockwave rings
        for (int sw = 0; sw < 2; sw++) {
            float swPhase = fract(time * 0.8 + float(sw) * 0.5);
            float swRadius = swPhase * 0.6;
            float swAge = swPhase;
            float swDist = abs(length(uv - vec2(0.5)) - swRadius);
            float swMask = smoothstep(0.015, 0.0, swDist) * (1.0 - swAge * swAge);
            swMask *= bassEnv * 2.0;
            vec3 swCol = mix(palPrimary, palAccent, swPhase);
            col += swCol * swMask * 0.6;
        }
    }

    // (treble flash merged into main dot loop above)

    return col;
}


// =========================================================================
//  MAIN ZONE RENDER
// =========================================================================

vec4 renderEosZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params,
                   bool isHighlighted, float bass, float mids, float treble, float overall,
                   bool hasAudio) {
    float borderRadius = max(params.x, 8.0), borderWidth = max(params.y, 2.0);
    float speed      = customParams[0].x >= 0.0 ? customParams[0].x : 0.10;
    float flowSpeed  = customParams[0].y >= 0.0 ? customParams[0].y : 0.20;
    float noiseScale = customParams[0].z >= 0.0 ? customParams[0].z : 3.5;
    int octaves      = int(customParams[0].w >= 0.0 ? customParams[0].w : 6.0);
    float gridScale    = customParams[1].x >= 0.0 ? customParams[1].x : 5.0;
    float gridStrength = customParams[1].y >= 0.0 ? customParams[1].y : 0.20;
    float brightness   = customParams[1].z >= 0.0 ? customParams[1].z : 0.7;
    float contrast     = customParams[1].w >= 0.0 ? customParams[1].w : 0.9;
    float fillOpacity      = customParams[2].x >= 0.0 ? customParams[2].x : 0.85;
    float borderGlow       = customParams[2].y >= 0.0 ? customParams[2].y : 0.35;
    float edgeFadeStart    = customParams[2].z >= 0.0 ? customParams[2].z : 30.0;
    float borderBrightness = customParams[2].w >= 0.0 ? customParams[2].w : 1.4;
    float audioReact   = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;
    float particleStr  = customParams[3].y >= 0.0 ? customParams[3].y : 0.4;
    float innerGlowStr = customParams[3].z >= 0.0 ? customParams[3].z : 0.35;
    float sparkleStr   = customParams[3].w >= 0.0 ? customParams[3].w : 2.0;
    float connThreshold = customParams[5].x >= 0.0 ? customParams[5].x : 0.25;
    float logoScale     = customParams[5].y >= 0.0 ? customParams[5].y : 0.5;
    float logoIntensity = customParams[5].z >= 0.0 ? customParams[5].z : 0.75;
    float logoPulse     = customParams[5].w >= 0.0 ? customParams[5].w : 0.8;
    int   logoCount   = clamp(int(customParams[6].x >= 0.0 ? customParams[6].x : 3.0), 1, 8);
    float logoSizeMin = customParams[6].y >= 0.0 ? customParams[6].y : 0.4;
    float logoSizeMax = customParams[6].z >= 0.0 ? customParams[6].z : 1.0;
    float gradCenterX = customParams[6].w >= -1.5 ? customParams[6].w : 0.5;
    float gradCenterY = customParams[7].x >= -1.5 ? customParams[7].x : 0.5;
    float gradAngle   = customParams[4].w >= 0.0 ? customParams[4].w : 0.6;
    float idleStrength = customParams[7].w >= 0.0 ? customParams[7].w : 0.5;

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;

    vec2 p = fragCoord - center;
    float d = sdRoundedBox(p, halfSize, borderRadius);
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    float time = iTime;

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, EOS_PERI);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, EOS_PURPLE);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, EOS_CORAL);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, EOS_GLOW);

    float vitality = isHighlighted ? 1.0 : 0.3;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(time * 0.8 * PI)) * idleStrength;

    float bassEnv   = hasAudio ? smoothstep(0.02, 0.25, bass) * audioReact : 0.0;
    float midsEnv   = hasAudio ? smoothstep(0.02, 0.4, mids) * audioReact : 0.0;
    float trebleEnv = hasAudio ? smoothstep(0.05, 0.5, treble) * audioReact : 0.0;

    vec4 result = vec4(0.0);

    if (d < 0.0) {

        // =============================================================
        //  BACKGROUND: Warm gradient wash + Constellation network
        // =============================================================

        // Warm gradient fill: purple (top-left) to coral (bottom-right)
        // This IS the background — rich and visible, not a faint tint
        float gradT = dot(globalUV, vec2(cos(gradAngle), sin(gradAngle)));
        vec3 gradBase = mix(palSecondary * 0.35, palAccent * 0.25, smoothstep(0.0, 1.0, gradT));
        vec3 col = gradBase * brightness;

        // Simplex noise color wash — adds movement and texture to the gradient
        {
            float n1 = simplex2D(globalUV * 3.0 + time * speed * 0.3) * 0.5 + 0.5;
            float n2 = simplex2D(globalUV * 2.0 + time * speed * 0.2 + vec2(50.0)) * 0.5 + 0.5;
            vec3 washCol = paletteSweep(n1 * contrast + midsEnv * 0.15,
                                         palPrimary, palSecondary, palAccent, palGlow, midsEnv);
            col = mix(col, washCol * brightness * 0.5, n2 * 0.4);
        }

        // Purple wash at top, coral warmth at bottom
        col += palSecondary * 0.1 * (1.0 - globalUV.y) * brightness;
        col += palAccent * 0.06 * globalUV.y * brightness;

        // -- Constellation network (primary feature) --
        {
            float orbitTime = time * flowSpeed;
            vec3 network = constellationNetwork(globalUV, orbitTime, bassEnv, midsEnv, trebleEnv,
                                                particleStr * 2.5, connThreshold,
                                                gridScale / 5.0, gridStrength * 5.0,
                                                vec2(gradCenterX, gradCenterY),
                                                palPrimary, palSecondary, palAccent, palGlow);
            col += network * brightness * 1.5;
        }

        // Ambient noise texture
        {
            float noise = simplex2D(globalUV * 35.0 + time * 0.08) * 0.5 + 0.5;
            col += palGlow * noise * 0.03 * brightness;
        }

        // Bass: whole-scene color flash toward warm coral on heavy hits
        col = mix(col, col + palAccent * 0.15, bassEnv * 0.6);
        // Bass breathing
        col *= (1.0 + bassEnv * 0.4);

        // Mids: palette warmth shift — purple-heavy music shifts toward warm coral
        col = mix(col, col * mix(vec3(1.0), palAccent * 1.5, 0.15), midsEnv * 0.4);

        // Treble: sharp bright flicker across the background (branchless)
        {
            vec2 flickerGrid = floor(globalUV * 30.0 + time * 8.0);
            float flicker = step(0.92, hash21(flickerGrid)) * trebleEnv;
            col += palGlow * flicker * 0.4;
        }

        // =============================================================
        //  MULTI-INSTANCE LOGO RENDERING (3 overlapping sails)
        // =============================================================

        for (int li = 0; li < logoCount && li < 8; li++) {
            float instScale;
            vec2 iLogoUV = computeInstanceUV(li, logoCount, globalUV, aspect, time,
                                              logoScale, bassEnv, logoPulse,
                                              logoSizeMin, logoSizeMax, instScale);

            if (iLogoUV.x < -0.3 || iLogoUV.x > 1.3 ||
                iLogoUV.y < -0.2 || iLogoUV.y > 1.1) continue;

            // AABB early-out
            {
                vec2 dLo = vec2(-0.25) - iLogoUV;
                vec2 dHi = iLogoUV - vec2(1.25, 1.1);
                vec2 outerDist = max(max(dLo, dHi), vec2(0.0));
                if (dot(outerDist, outerDist) > 0.0625) continue;
            }

            float maxScale = logoSizeMax * logoScale;
            float depthFactor = clamp(instScale / max(maxScale, 0.01), 0.0, 1.0);
            float instIntensity = logoIntensity * (0.3 + 0.7 * depthFactor);

            float iSeed = float(li) * 1.618;
            float iHash = hash21(vec2(float(li) * 7.31, 3.17));
            float iPhase = iHash * TAU;

            // Evaluate per-sail hit (also computes union SDF and overlap count)
            SailHit sail = evalEosSails(iLogoUV, palPrimary, palAccent, palSecondary);
            float unionDist = sail.unionDist;
            if (unionDist > 0.25) continue;

            vec3 logoCol = vec3(0.0);
            vec3 outerCol = vec3(0.0);

            vec2 logoP = iLogoUV - LOGO_CENTER;
            float logoR = length(logoP);
            float logoVignette = 1.0 - smoothstep(0.45, 0.75, logoR);

            // Light casting onto background from logo
            float lightCast = exp(-max(unionDist, 0.0) * 10.0) * 0.35;
            vec3 logoLight = paletteSweep(time * 0.08 + iLogoUV.y + float(li) * 0.3,
                                           palPrimary, palSecondary, palAccent, palGlow, midsEnv);
            col += logoLight * lightCast * instIntensity * (1.0 + bassEnv * 0.3) * depthFactor;

            // =====================================================
            //  OUTER EFFECTS: WARM RADIAL GLOW + NEAR-LOGO DOT BOOST
            // =====================================================
            if (unionDist > -0.02 && unionDist < 0.20) {
                // Warm radial glow
                if (unionDist > 0.0) {
                    float emergeRadius = 0.10 + bassEnv * 0.04;
                    float emergeFalloff = exp(-unionDist / emergeRadius) * 0.35;
                    vec3 emergeCol = mix(palGlow, palPrimary, 0.3);
                    outerCol += emergeCol * emergeFalloff * instIntensity * depthFactor;

                    // Subtle orbit rings
                    for (int ri = 0; ri < 2; ri++) {
                        float ringPhase = fract(time * 0.3 + float(ri) * 0.5 + float(li) * 0.17);
                        float ringRadius = ringPhase * 0.18;
                        float ringDist = abs(unionDist - ringRadius);
                        float ringMask = smoothstep(0.006, 0.0, ringDist) * (1.0 - ringPhase * ringPhase);
                        ringMask *= bassEnv * 1.2 + 0.15;
                        vec3 ringCol = paletteSweep(ringPhase + float(ri) * 0.25,
                                                     palPrimary, palSecondary, palAccent, palGlow, midsEnv);
                        outerCol += ringCol * ringMask * 0.4 * depthFactor;
                    }
                }
            }

            // =====================================================
            //  LOGO INTERIOR: TRI-SAIL INTERFERENCE PATTERN
            //  Each sail flows in a different direction (120° apart).
            //  Where sails overlap, their flows blend and create
            //  interference — a pattern unique to this 3-sail logo.
            // =====================================================
            if (sail.sailIndex >= 0) {
                float fDist = sail.dist;
                float depth = clamp(-fDist / 0.12, 0.0, 1.0);

                // Per-sail phase offset for bass pulse (120° apart)
                // Hoist trig: sailAngle == sailPhaseOffset, compute cos/sin once
                float sailAngle = float(sail.sailIndex) * TAU / 3.0;
                float sailPulse = 1.0 + bassEnv * logoPulse * 0.6
                                  * (0.5 + 0.5 * sin(time * 2.0 + sailAngle + iPhase));

                // --- Layer 1: Per-sail directional flow ---
                // Each sail's noise flows in a unique direction (0°, 120°, 240°)
                vec2 sailFlowDir = vec2(cos(sailAngle), sin(sailAngle));
                vec2 flowUV = iLogoUV * noiseScale * 1.5 + iSeed * 10.0;
                flowUV += sailFlowDir * time * speed * 2.0;

                // Domain warp for organic motion (cache shared subexpression)
                vec2 warpBase = flowUV * 0.6 + time * 0.1;
                float warp1 = simplex2D(warpBase) * 0.4;
                float warp2 = simplex2D(warpBase + vec2(50.0)) * 0.4;
                flowUV += vec2(warp1, warp2) * 0.5;

                float flow = simplexFBM(flowUV, min(max(octaves - 2, 3), 4));

                // Base: sail color modulated by flow
                vec3 interiorCol = mix(sail.sailColor * 0.4, sail.sailColor, flow) * brightness * 1.4;

                // --- Layer 2: Overlap interference pattern ---
                // Where 2+ sails share space, blend their directional flows.
                // Unrolled loop with constant trig; branch avoids 3 simplex calls
                // for the majority of pixels that are in only 1 sail.
                if (sail.overlapCount >= 2) {
                    // Constant sail directions (120 deg apart) -- no trig needed
                    const vec2 sDir0 = vec2(1.0, 0.0);                       // cos(0), sin(0)
                    const vec2 sDir1 = vec2(-0.5, 0.86602540378);            // cos(2pi/3), sin(2pi/3)
                    const vec2 sDir2 = vec2(-0.5, -0.86602540378);           // cos(4pi/3), sin(4pi/3)
                    vec2 baseUV = iLogoUV * noiseScale * 1.5 + iSeed * 10.0;
                    float tOff = time * speed * 2.0;
                    vec2 timeOff = vec2(time * 0.15);
                    float sFlow0 = simplex2D((baseUV + sDir0 * tOff) * 2.0 + timeOff) * 0.5 + 0.5;
                    float sFlow1 = simplex2D((baseUV + sDir1 * tOff) * 2.0 + timeOff) * 0.5 + 0.5;
                    float sFlow2 = simplex2D((baseUV + sDir2 * tOff) * 2.0 + timeOff) * 0.5 + 0.5;
                    vec3 blendCol = (palPrimary * sFlow0 + palAccent * sFlow1 + palSecondary * sFlow2) / 3.0;
                    float interference = (sFlow0 + sFlow1 + sFlow2) / 3.0;
                    float interfereStr = abs(interference - 0.5) * 2.0;
                    interfereStr *= interfereStr;

                    float overlapStr = float(sail.overlapCount - 1) * 0.5;
                    overlapStr *= (1.0 + trebleEnv + midsEnv * 0.5);

                    interiorCol = mix(interiorCol, blendCol * brightness * 1.5, overlapStr * 0.5);
                    interiorCol += palGlow * interfereStr * overlapStr * 0.4 * brightness;
                }

                // --- Layer 3: Summit convergence beacon ---
                {
                    vec2 summitPt = vec2(0.584, 0.095);
                    float summitDist = length(iLogoUV - summitPt);
                    float beaconGlow = exp(-summitDist * 8.0) * 0.9;
                    float beaconPulse = 0.5 + 0.5 * pow(max(sin(time * 1.3 + iPhase), 0.0), 2.0);
                    beaconPulse *= 1.0 + bassEnv * 1.5;
                    // Tri-color convergence: all 3 sail colors blend at the summit
                    vec3 beaconCol = (palPrimary + palSecondary + palAccent) / 3.0;
                    beaconCol = mix(beaconCol, palGlow, 0.4);
                    interiorCol += beaconCol * beaconGlow * beaconPulse;
                }

                // --- Layer 4: Edge energy with sail-colored traveling pulse ---
                {
                    float edgeProximity = sail.edgeDist;
                    float edgeWidth = 0.006 + bassEnv * 0.004;
                    float edgeGlow = smoothstep(edgeWidth, edgeWidth * 0.1, edgeProximity);

                    // Pulse travels along edges in the sail's flow direction
                    float pulsePhase = dot(iLogoUV, sailFlowDir) * 12.0 - time * 3.0 + iPhase;
                    float pulse = pow(max(sin(pulsePhase), 0.0), 3.0);
                    edgeGlow *= 0.3 + pulse * 0.7;

                    // Branchless treble edge spark (trebleEnv multiplier handles zero case)
                    {
                        float edgeSpark = simplex2D(iLogoUV * 35.0 + time * 5.0 + iSeed * 20.0);
                        edgeSpark = smoothstep(0.5, 0.9, edgeSpark) * trebleEnv;
                        edgeGlow += edgeSpark * smoothstep(edgeWidth * 2.0, 0.0, edgeProximity);
                    }

                    interiorCol += sail.sailColor * edgeGlow * 1.0;
                }

                // --- Interior compositing ---
                interiorCol *= sailPulse;

                // Bass: sail flashes toward white on heavy hits (dramatic impact)
                interiorCol = mix(interiorCol, interiorCol + sail.sailColor * 0.5, bassEnv * 0.5);

                // Treble: bright crackling energy across interior (branchless)
                {
                    float crackle = simplex2D(iLogoUV * 20.0 + time * 10.0 + iSeed * 5.0);
                    crackle = smoothstep(0.4, 0.9, crackle) * trebleEnv;
                    interiorCol += palGlow * crackle * 0.6;
                }

                // Mids: visible color cycling — sails shift toward complementary color
                vec3 midsShift = sail.sailIndex == 0 ? palAccent :
                                 sail.sailIndex == 1 ? palPrimary : palSecondary;
                interiorCol = mix(interiorCol, interiorCol * mix(vec3(1.0), midsShift, 0.3), midsEnv * 0.5);

                // Rim light
                float rimLight = exp(-depth * 5.0);
                interiorCol += palGlow * rimLight * 0.3 * (1.0 + midsEnv * 0.5);

                // Fresnel edge highlight — bass widens it
                float fresnelEdge = -0.012 - bassEnv * 0.005;
                float fresnelLike = smoothstep(fresnelEdge, -0.001, fDist);
                interiorCol += palGlow * fresnelLike * 0.4 * (1.0 + bassEnv * 0.5);

                // Reinhard tonemap
                interiorCol = interiorCol / (1.0 + interiorCol);
                float lum = luminance(interiorCol);
                interiorCol = mix(vec3(lum), interiorCol, 1.4);
                interiorCol = max(interiorCol, vec3(0.0));

                float aa = smoothstep(0.0, -0.003, fDist);
                logoCol = mix(logoCol, interiorCol * instIntensity, aa);
            }

            // -- Edge discharge on treble --
            if (trebleEnv > 0.01 && unionDist > -0.004 && unionDist < 0.018) {
                float sparkN = simplex2D(iLogoUV * 30.0 + time * 5.0 + float(li) * 33.0) * 0.5 + 0.5;
                sparkN = smoothstep(0.5, 0.92, sparkN);
                float edgeMask = smoothstep(0.018, 0.0, abs(unionDist));
                col += palGlow * sparkN * edgeMask * trebleEnv * sparkleStr * depthFactor;
            }

            // -- Multi-layer glow --
            if (unionDist > -0.01 && unionDist < 0.20) {
                float clampedDist = max(unionDist, 0.0);
                float glow1 = exp(-clampedDist * 60.0) * 0.5;
                float glow2 = exp(-clampedDist * 18.0) * 0.25;
                float glow3 = exp(-clampedDist * 5.0) * 0.12;
                vec3 edgeCol = paletteSweep(time * 0.1 + iLogoUV.y * 0.5 + float(li) * 0.25,
                                             palPrimary, palSecondary, palAccent, palGlow, midsEnv);
                float flare = 1.0 + bassEnv * 0.4;
                col += edgeCol * glow1 * flare * particleStr * 2.0 * depthFactor;
                col += palGlow * glow2 * flare * 0.5 * depthFactor;
                col += palAccent * glow3 * 0.4 * depthFactor;
            }

            // -- Two-pass composite --
            float fillAlpha = smoothstep(0.005, -0.005, unionDist);

            // Pass 1: inside logo -- opaque fill replaces background
            col = mix(col, logoCol, fillAlpha);

            // Pass 2: outside logo -- dim background near logo, add outer effects
            float outerMask = (1.0 - fillAlpha) * logoVignette;
            float proximityDim = smoothstep(0.20, 0.0, unionDist) * 0.5;
            col *= 1.0 - proximityDim * outerMask;
            col += outerCol * outerMask;

        } // end logo instance loop

        // -- Vitality --
        if (isHighlighted) {
            col *= 1.1;
        } else {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.25);
            col *= 0.7 + idlePulse * 0.08;
        }

        // -- Inner edge glow --
        float innerDist = -d;
        float depthDarken = smoothstep(0.0, edgeFadeStart, innerDist);
        col *= mix(0.6, 1.0, 1.0 - depthDarken * 0.35);

        float innerGlow = exp(-innerDist / 12.0);
        float edgeAngle = atan(p.y, p.x);
        float iriT = edgeAngle / TAU + time * 0.04 + midsEnv * 0.15;
        vec3 iriCol = paletteSweep(iriT, palPrimary, palSecondary, palAccent, palGlow, midsEnv);
        col += iriCol * innerGlow * innerGlowStr;

        col = mix(col, fillColor.rgb * luminance(col), 0.15);

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // -- Border --
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float angle = atan(p.y, p.x);
        float borderElev = simplex2D(vec2(sin(angle), cos(angle)) * 3.0 + time * 0.15) * 0.5 + 0.5;
        vec3 borderCol = paletteSweep(borderElev * contrast + midsEnv * 0.2,
                                       palPrimary, palSecondary, palAccent, palGlow, midsEnv);
        vec3 zoneBorderTint = colorWithFallback(borderColor.rgb, borderCol);
        borderCol = mix(borderCol, zoneBorderTint * luminance(borderCol), 0.25);
        borderCol *= borderBrightness;

        if (isHighlighted) {
            float bBreathe = 0.85 + 0.15 * sin(time * 2.5);
            float borderBass = hasAudio ? 1.0 + bassEnv * 0.3 : 1.0;
            borderCol *= bBreathe * borderBass;
        } else {
            float lum = luminance(borderCol);
            borderCol = mix(borderCol, vec3(lum), 0.3);
            borderCol *= 0.55;
        }

        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }

    // -- Outer glow --
    float bassGlowPush = hasAudio ? bassEnv * 2.0 : idlePulse * 5.0;
    float glowRadius = mix(10.0, 18.0, vitality) + bassGlowPush;
    if (d > 0.0 && d < glowRadius && borderGlow > 0.01) {
        float glow = expGlow(d, 7.0, borderGlow);
        float angle = atan(p.y, p.x);
        float glowT = angularNoise(angle, 1.5, time * 0.06) + midsEnv * 0.1;
        vec3 glowCol = paletteSweep(glowT, palPrimary, palSecondary, palAccent, palGlow, midsEnv);
        glowCol *= mix(0.3, 1.0, vitality);
        result.rgb += glowCol * glow * 0.5;
        result.a = max(result.a, glow * 0.4);
    }

    return result;
}


// =========================================================================
//  LABEL COMPOSITING
// =========================================================================

vec4 compositeEosLabels(vec4 color, vec2 fragCoord,
                        float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, EOS_PERI);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, EOS_PURPLE);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, EOS_CORAL);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, EOS_GLOW);

    float labelGlowSpread = customParams[4].x >= 0.0 ? customParams[4].x : 3.0;
    float labelBrightness = customParams[4].y >= 0.0 ? customParams[4].y : 2.5;
    float labelAudioReact = customParams[4].z >= 0.0 ? customParams[4].z : 1.0;

    float time = iTime;

    float bassR   = hasAudio ? bass * labelAudioReact   : 0.0;
    float midsR   = hasAudio ? mids * labelAudioReact   : 0.0;
    float trebleR = hasAudio ? treble * labelAudioReact : 0.0;

    // -- 3-pass Gaussian halo --
    float haloSmooth = 0.0;
    float haloWide = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float r2 = float(dx * dx + dy * dy);
            vec2 offTight = vec2(float(dx), float(dy)) * px * labelGlowSpread;
            float s = texture(uZoneLabels, uv + offTight).a;
            haloSmooth += s * exp(-r2 * 0.4);

            vec2 offWide = vec2(float(dx), float(dy)) * px * labelGlowSpread * 1.5;
            float sw = texture(uZoneLabels, uv + offWide).a;
            haloWide += sw * exp(-r2 * 0.15);
        }
    }
    haloSmooth /= 8.0;
    haloWide   /= 12.0;

    float haloAtmo = 0.0;
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            float r2 = float(dx * dx + dy * dy);
            vec2 off = vec2(float(dx), float(dy)) * px * labelGlowSpread * 1.8;
            haloAtmo += texture(uZoneLabels, uv + off).a * exp(-r2 * 0.12) * step(r2, 12.0);
        }
    }
    haloAtmo /= 18.0;

    if (haloAtmo > 0.001) {
        float noText = 1.0 - labels.a;
        float outerMask = haloAtmo * noText, innerMask = haloSmooth * noText, midMask = haloWide * noText;
        vec3 haloCol = mix(palPrimary, palGlow, 0.4);
        color.rgb += haloCol * innerMask * (2.0 + bassR) + haloCol * midMask * (1.0 + bassR * 0.5);
        color.rgb += mix(haloCol, palAccent, 0.25) * outerMask * (0.5 + bassR * 0.3);
        float haloAngle = atan(uv.y - 0.5, uv.x - 0.5);
        float rayAngle = mod(haloAngle + PI / 3.0, TAU / 3.0) - TAU / 6.0;
        float ray = exp(-abs(rayAngle) * (20.0 + trebleR * 30.0));
        color.rgb += palPrimary * 1.2 * ray * midMask * (0.6 + 0.4 * sin(time * 1.8 + haloAngle * 3.0));
        if (trebleR > 0.04)
            color.rgb += palAccent * innerMask * step(0.85, hash21(floor(uv * 70.0 + time * 1.5))) * trebleR * 2.5;
        color.a = max(color.a, midMask * 0.8);
    }

    // -- Text body --
    if (labels.a > 0.01) {
        float warmGlow = 0.9 + 0.1 * sin(fragCoord.y * 0.5 + time * 0.3);
        float aL = texture(uZoneLabels, uv + vec2(-px.x, 0.0)).a;
        float aR = texture(uZoneLabels, uv + vec2( px.x, 0.0)).a;
        float aU = texture(uZoneLabels, uv + vec2(0.0, -px.y)).a;
        float aD = texture(uZoneLabels, uv + vec2(0.0,  px.y)).a;
        float edgeStrength = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);
        vec3 textCol = mix(palPrimary, palGlow, uv.x * 0.3 + uv.y * 0.2 + 0.25);
        float cPos = fract(time * (0.12 + bassR * 0.2));
        float cX = fragCoord.x / max(iResolution.x, 1.0);
        float cDist = min(abs(cX - cPos), 1.0 - abs(cX - cPos));
        textCol *= warmGlow;
        textCol = mix(textCol, palAccent * 1.8, smoothstep(0.03, 0.0, cDist) * 0.3);
        textCol += palGlow * edgeStrength * 0.6;
        textCol *= (1.0 + bassR * 0.3) * labelBrightness;
        if (trebleR > 0.04) {
            float spark = step(0.88, hash21(floor(uv * 70.0 + time * 2.5))) * trebleR;
            textCol = mix(textCol, palAccent * 2.0, spark * 0.4);
        }
        textCol = textCol / (0.6 + textCol);
        textCol = max(mix(vec3(dot(textCol, vec3(0.2126, 0.7152, 0.0722))), textCol, 1.3), vec3(0.0));
        color.rgb = mix(color.rgb, textCol, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}


// =========================================================================
//  ENTRY POINT
// =========================================================================

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) { fragColor = vec4(0.0); return; }

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    float overall = getOverallSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        vec4 zoneColor = renderEosZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (customParams[7].y > 0.5)
        color = compositeEosLabels(color, fragCoord, bass, mids, treble, hasAudio);
    fragColor = clampFragColor(color);
}
