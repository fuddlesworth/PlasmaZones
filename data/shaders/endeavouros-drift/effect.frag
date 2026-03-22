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

const vec2 LOGO_CENTER = vec2(0.50, 0.42);


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
// Blue sail (#7f7fff) -- 26 vertices
const int EOS_BLUE_N = 26;
const vec2 EOS_BLUE_POLY[26] = vec2[26](
    vec2(0.593859, 0.000344),
    vec2(0.676948, 0.117089),
    vec2(0.785576, 0.286462),
    vec2(0.874195, 0.471311),
    vec2(0.897259, 0.634483),
    vec2(0.809222, 0.738825),
    vec2(0.690739, 0.757278),
    vec2(0.515398, 0.752583),
    vec2(0.331622, 0.736806),
    vec2(0.187831, 0.722009),
    vec2(0.132446, 0.720258),
    vec2(0.109506, 0.758077),
    vec2(0.089652, 0.790848),
    vec2(0.074039, 0.816643),
    vec2(0.063824, 0.833533),
    vec2(0.060163, 0.839592),
    vec2(0.107759, 0.841259),
    vec2(0.233657, 0.843569),
    vec2(0.412519, 0.842483),
    vec2(0.619009, 0.833964),
    vec2(0.827788, 0.813973),
    vec2(1.000000, 0.717936),
    vec2(0.990988, 0.539531),
    vec2(0.872478, 0.327781),
    vec2(0.716193, 0.131711),
    vec2(0.593859, 0.000344)
);

// Coral sail (#ff7f7f) -- 16 vertices
const int EOS_RED_N = 16;
const vec2 EOS_RED_POLY[16] = vec2[16](
    vec2(0.592812, 0.000013),
    vec2(0.519376, 0.073964),
    vec2(0.370885, 0.246626),
    vec2(0.199832, 0.451852),
    vec2(0.058706, 0.623496),
    vec2(0.000000, 0.695410),
    vec2(0.004936, 0.696709),
    vec2(0.020107, 0.700301),
    vec2(0.046054, 0.705728),
    vec2(0.083320, 0.712533),
    vec2(0.132446, 0.720258),
    vec2(0.190678, 0.635369),
    vec2(0.304098, 0.453575),
    vec2(0.434544, 0.242234),
    vec2(0.543852, 0.068704),
    vec2(0.593859, 0.000344)
);

// Purple sail (#7f3fbf) -- 16 vertices
const int EOS_PURP_N = 16;
const vec2 EOS_PURP_POLY[16] = vec2[16](
    vec2(0.593363, 0.000123),
    vec2(0.538002, 0.073790),
    vec2(0.422766, 0.252230),
    vec2(0.289120, 0.465850),
    vec2(0.178525, 0.645057),
    vec2(0.132446, 0.720258),
    vec2(0.175451, 0.724642),
    vec2(0.284072, 0.734991),
    vec2(0.427717, 0.747105),
    vec2(0.575794, 0.756780),
    vec2(0.697711, 0.759815),
    vec2(0.875925, 0.691467),
    vec2(0.893124, 0.530063),
    vec2(0.810832, 0.326886),
    vec2(0.690569, 0.133219),
    vec2(0.593859, 0.000344)
);

const vec2 EOS_AABB_LO = vec2(0.000, 0.000);
const vec2 EOS_AABB_HI = vec2(1.000, 0.845);

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

// SDF with AABB early-out returning vec2(signedDist, edgeDist)
vec2 sdBlueWithEdge(vec2 p) {
    vec2 dLo = EOS_AABB_LO - p; vec2 dHi = p - EOS_AABB_HI;
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.04) { float bd = sqrt(boxDist2); return vec2(bd, bd); }
    POLY_SDF_BODY(EOS_BLUE_POLY, EOS_BLUE_N)
    return vec2(s * edgeDist, edgeDist);
}
vec2 sdRedWithEdge(vec2 p) {
    vec2 dLo = EOS_AABB_LO - p; vec2 dHi = p - EOS_AABB_HI;
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.04) { float bd = sqrt(boxDist2); return vec2(bd, bd); }
    POLY_SDF_BODY(EOS_RED_POLY, EOS_RED_N)
    return vec2(s * edgeDist, edgeDist);
}
vec2 sdPurpWithEdge(vec2 p) {
    vec2 dLo = EOS_AABB_LO - p; vec2 dHi = p - EOS_AABB_HI;
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.04) { float bd = sqrt(boxDist2); return vec2(bd, bd); }
    POLY_SDF_BODY(EOS_PURP_POLY, EOS_PURP_N)
    return vec2(s * edgeDist, edgeDist);
}


// -- Sail hit result ------------------------------------------------------

struct SailHit {
    float dist;       // signed distance
    float edgeDist;   // always positive edge distance
    int sailIndex;    // 0=red(coral), 1=purple, 2=blue -- painter's order
    vec3 sailColor;   // fill color for this sail
};

// Evaluate all 3 sails at point p, return the topmost hit (painter's order)
SailHit evalEosSails(vec2 p, vec3 colBlue, vec3 colCoral, vec3 colPurple) {
    // Painter's order: coral first (back), purple second, blue on top (front)
    vec2 redResult = sdRedWithEdge(p);
    vec2 purpResult = sdPurpWithEdge(p);
    vec2 blueResult = sdBlueWithEdge(p);

    SailHit hit;
    hit.dist = min(redResult.x, min(purpResult.x, blueResult.x));
    hit.edgeDist = min(redResult.y, min(purpResult.y, blueResult.y));
    hit.sailIndex = -1;
    hit.sailColor = vec3(0.0);

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

// Minimum signed distance across all sails (for outer effects)
float sdEosUnion(vec2 p) {
    return min(sdRedWithEdge(p).x, min(sdPurpWithEdge(p).x, sdBlueWithEdge(p).x));
}

// Count how many sails overlap at point p (0-3)
int sailOverlapCount(vec2 p) {
    int count = 0;
    if (sdRedWithEdge(p).x < 0.0) count++;
    if (sdPurpWithEdge(p).x < 0.0) count++;
    if (sdBlueWithEdge(p).x < 0.0) count++;
    return count;
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

const int CONSTELLATION_COUNT = 24;

vec3 constellationNetwork(vec2 uv, float time, float bassEnv, float midsEnv, float trebleEnv,
                          float particleStr, float connThreshold,
                          vec3 palPrimary, vec3 palSecondary, vec3 palAccent, vec3 palGlow) {
    vec3 col = vec3(0.0);
    vec2 dots[24]; vec3 dotColors[24];
    // Bass makes dots scatter outward from center (explosive feel)
    float scatter = 1.0 + bassEnv * 0.6;
    for (int i = 0; i < CONSTELLATION_COUNT; i++) {
        float fi = float(i);
        float h1=hash21(vec2(fi*7.13,3.91)), h2=hash21(vec2(fi*11.37,17.53)), h3=hash21(vec2(fi*5.79,23.11));
        dots[i] = vec2(0.5 + sin(time*(0.12+h1*0.18)+h1*TAU)*(0.32+h3*0.15)*scatter,
                        0.5 + cos(time*(0.10+h2*0.16)+h2*TAU)*(0.28+h1*0.12)*scatter);
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

                // Lines brighten on bass
                float lineBass = 0.6 + bassEnv * 1.5;

                float linePhase = fract(time * 0.25 + hash21(vec2(fi, float(j))) * TAU);
                float linePulse = 0.5 + 0.5 * sin(linePhase * TAU);

                vec3 lineCol = mix(dotColors[i], dotColors[j], 0.5);
                col += lineCol * 0.5 * lineMask * proximity * linePulse * lineBass * particleStr;
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

    // ── Treble flash: random dots burst bright on treble ──
    if (trebleEnv > 0.05) {
        for (int i = 0; i < CONSTELLATION_COUNT; i++) {
            float fi = float(i);
            // Each dot has a chance to flash on treble
            float flashChance = step(0.5, hash21(vec2(fi, floor(time * 6.0))));
            if (flashChance > 0.5) {
                float dist = length(uv - dots[i]);
                float flash = exp(-dist * 30.0) * trebleEnv * 2.0;
                col += dotColors[i] * flash;
            }
        }
    }

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
        float gradT = globalUV.x * 0.5 + globalUV.y * 0.5;
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

        // Treble: sharp bright flicker across the background
        if (trebleEnv > 0.1) {
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

            // Union SDF for outer effects
            float unionDist = sdEosUnion(iLogoUV);
            if (unionDist > 0.25) continue;

            // Evaluate per-sail hit
            SailHit sail = evalEosSails(iLogoUV, palPrimary, palAccent, palSecondary);

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
                float sailPhaseOffset = float(sail.sailIndex) * TAU / 3.0;
                float sailPulse = 1.0 + bassEnv * logoPulse * 0.6
                                  * (0.5 + 0.5 * sin(time * 2.0 + sailPhaseOffset + iPhase));

                // --- Layer 1: Per-sail directional flow ---
                // Each sail's noise flows in a unique direction (0°, 120°, 240°)
                float sailAngle = float(sail.sailIndex) * TAU / 3.0;
                vec2 sailFlowDir = vec2(cos(sailAngle), sin(sailAngle));
                vec2 flowUV = iLogoUV * 5.0 + iSeed * 10.0;
                flowUV += sailFlowDir * time * speed * 2.0;

                // Domain warp for organic motion
                float warp1 = simplex2D(flowUV * 0.6 + time * 0.1) * 0.4;
                float warp2 = simplex2D(flowUV * 0.6 + time * 0.1 + vec2(50.0)) * 0.4;
                flowUV += vec2(warp1, warp2) * 0.5;

                float flow = simplexFBM(flowUV, max(octaves - 1, 3));

                // Base: sail color modulated by flow
                vec3 interiorCol = mix(sail.sailColor * 0.4, sail.sailColor, flow) * brightness * 1.4;

                // --- Layer 2: Overlap interference pattern ---
                // Where 2+ sails share space, blend their directional flows
                {
                    int overlapN = sailOverlapCount(iLogoUV);
                    if (overlapN >= 2) {
                        // Compute the OTHER sails' flows too
                        vec3 blendCol = vec3(0.0);
                        float interference = 0.0;
                        for (int si = 0; si < 3; si++) {
                            float sAngle = float(si) * TAU / 3.0;
                            vec2 sDir = vec2(cos(sAngle), sin(sAngle));
                            vec2 sUV = iLogoUV * 5.0 + iSeed * 10.0 + sDir * time * speed * 2.0;
                            float sFlow = simplex2D(sUV * 2.0 + time * 0.15) * 0.5 + 0.5;
                            vec3 sCol = si == 0 ? palPrimary : si == 1 ? palAccent : palSecondary;
                            blendCol += sCol * sFlow;
                            interference += sFlow;
                        }
                        blendCol /= 3.0;
                        // Interference creates bright bands where flows align
                        float interfereStr = abs(interference / 3.0 - 0.5) * 2.0;
                        interfereStr = pow(interfereStr, 2.0);

                        float overlapStr = float(overlapN - 1) * 0.5;
                        overlapStr *= (1.0 + trebleEnv * 1.0 + midsEnv * 0.5);

                        // Blend interference pattern into interior
                        interiorCol = mix(interiorCol, blendCol * brightness * 1.5, overlapStr * 0.5);
                        interiorCol += palGlow * interfereStr * overlapStr * 0.4 * brightness;
                    }
                }

                // --- Layer 3: Summit convergence beacon ---
                {
                    vec2 summitPt = vec2(0.593, 0.0003);
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

                    if (trebleEnv > 0.01) {
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

                // Treble: bright crackling energy across interior
                if (trebleEnv > 0.05) {
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
