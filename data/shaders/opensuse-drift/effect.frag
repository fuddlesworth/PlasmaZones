// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * OPENSUSE DRIFT - Fragment Shader (Iridescent Chameleon Skin)
 *
 * Unique visual identity built on chameleon biology:
 *   - Hexagonal reptile scale field with organic perturbation
 *   - Thin-film interference iridescence (nanocrystal color shifting)
 *   - Turing pattern overlays (organic chameleon markings)
 *   - Curl noise dust particles (Tumbleweed desert wind)
 *   - 82-vertex polygon SDF Geeko logo extracted from official SVG
 *
 * Audio reactivity (organic — modulates existing animation):
 *   Bass  = scale breathing + color metamorphosis speed + dust wind
 *          + chromatophore cascade + tail pulse + scale ripple wave
 *   Mids  = thin-film thickness shift (hue change) + Turing evolution
 *          + turret eye iris pulse + camouflage shimmer
 *   Treble = scale edge sparkle + dust particle bursts + logo flicker
 *           + nanocrystal prism bursts
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

// ── openSUSE official palette (Chameleon design system) ──────
const vec3 SUSE_TEAL  = vec3(0.090, 0.247, 0.310);   // #173f4f
const vec3 SUSE_GREEN = vec3(0.451, 0.729, 0.145);   // #73ba25
const vec3 SUSE_TURQ  = vec3(0.208, 0.725, 0.671);   // #35b9ab
const vec3 SUSE_GLOW  = vec3(0.588, 0.796, 0.361);   // #96cb5c

// ── Palette interpolation ────────────────────────────────────

vec3 susePalette(float t, vec3 primary, vec3 secondary, vec3 accent) {
    t = fract(t);
    if (t < 0.33)      return mix(primary, secondary, t * 3.0);
    else if (t < 0.66) return mix(secondary, accent, (t - 0.33) * 3.0);
    else               return mix(accent, primary, (t - 0.66) * 3.0);
}

// ── Thin-film iridescence ────────────────────────────────────
// Simulates constructive/destructive interference at three wavelengths.

vec3 thinFilm(float cosTheta, float thickness) {
    float delta = 2.0 * thickness * cosTheta;
    return 0.5 + 0.5 * vec3(
        cos(delta * TAU / 0.650),
        cos(delta * TAU / 0.510),
        cos(delta * TAU / 0.440)
    );
}

// ── Hexagonal scale grid ─────────────────────────────────────

struct ScaleCell {
    vec2 local;
    vec2 id;
    float edgeDist;
    float cellHash;
};

ScaleCell hexScaleGrid(vec2 p, float scale) {
    p *= scale;
    float sq3 = sqrt(3.0);
    vec2 size = vec2(1.0, sq3);
    vec2 halfSize = size * 0.5;

    // Compute cell IDs from floor() BEFORE local offset (stable integers)
    vec2 pA = p / size;
    vec2 pB = (p + halfSize) / size;
    vec2 idA = floor(pA);
    vec2 idB = floor(pB);
    vec2 a = (fract(pA) - 0.5) * size;
    vec2 b = (fract(pB) - 0.5) * size;

    bool useA = dot(a, a) < dot(b, b);
    vec2 gv = useA ? a : b;
    // Offset grid B IDs to avoid hash collisions between subgrids
    vec2 id = useA ? idA * 2.0 : idB * 2.0 + 1.0;

    float edgeDist = 0.5 - max(abs(gv.x), dot(abs(gv), vec2(0.5, sq3 * 0.5)));

    ScaleCell cell;
    cell.local = gv;
    cell.id = id;
    cell.edgeDist = edgeDist;
    cell.cellHash = hash21(cell.id);
    return cell;
}

// ── Single-pass Turing pattern approximation ─────────────────

float turingPattern(vec2 p, float time) {
    float pattern = 0.0;
    pattern += sin(p.x * 8.0 + sin(p.y * 6.0 + time) * 2.0);
    pattern += sin(p.y * 7.0 + sin(p.x * 5.0 - time * 0.7) * 2.0);
    pattern += sin((p.x + p.y) * 5.0 + time * 0.3) * 0.5;
    pattern += sin((p.x - p.y) * 6.0 - time * 0.4) * 0.5;
    return smoothstep(-0.2, 0.2, pattern * 0.25);
}

// ── 2D Curl noise ────────────────────────────────────────────

vec2 curlNoise(vec2 p, float time) {
    float eps = 0.01;
    float n  = noise2D(p + time * 0.1);
    float nx = noise2D(p + vec2(eps, 0.0) + time * 0.1);
    float ny = noise2D(p + vec2(0.0, eps) + time * 0.1);
    return vec2((ny - n) / eps, -(nx - n) / eps);
}

// ── FBM for domain-warped energy textures ─────────────────────

float fbm(vec2 uv, int octaves, float rotAngle) {
    float value = 0.0;
    float amplitude = 0.5;
    float c = cos(rotAngle), s = sin(rotAngle);
    mat2 rot = mat2(c, -s, s, c);
    for (int i = 0; i < octaves && i < 8; i++) {
        value += amplitude * noise2D(uv);
        uv = rot * uv * 2.0 + vec2(180.0);
        amplitude *= 0.55;
    }
    return value;
}

// ── Signed distance to line segment ─────────────────────────

float sdSegmentSuse(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}


// ═══════════════════════════════════════════════════════════════
//  GEEKO LOGO — 82-vertex polygon extracted from official SVG
//  Winding-number SDF (Inigo Quilez) with AABB early-out
// ═══════════════════════════════════════════════════════════════

const int GEEKO_N = 82;
const vec2 GEEKO[82] = vec2[82](
    vec2(0.9871, 0.4367),
    vec2(0.9934, 0.4318),
    vec2(0.9790, 0.3832),
    vec2(0.9454, 0.3241),
    vec2(0.8633, 0.2910),
    vec2(0.7811, 0.2724),
    vec2(0.7380, 0.2656),
    vec2(0.7370, 0.2979),
    vec2(0.5745, 0.2611),
    vec2(0.4342, 0.2561),
    vec2(0.3167, 0.2751),
    vec2(0.2485, 0.3001),
    vec2(0.1348, 0.3661),
    vec2(0.0770, 0.4185),
    vec2(0.0347, 0.4776),
    vec2(0.0081, 0.5561),
    vec2(0.0139, 0.6295),
    vec2(0.0516, 0.6936),
    vec2(0.1106, 0.7330),
    vec2(0.1906, 0.7442),
    vec2(0.2203, 0.7384),
    vec2(0.2681, 0.7101),
    vec2(0.2972, 0.6616),
    vec2(0.3010, 0.6171),
    vec2(0.2855, 0.5708),
    vec2(0.2483, 0.5331),
    vec2(0.2083, 0.5180),
    vec2(0.1710, 0.5186),
    vec2(0.1410, 0.5307),
    vec2(0.1162, 0.5573),
    vec2(0.1076, 0.5915),
    vec2(0.1185, 0.6278),
    vec2(0.1579, 0.6557),
    vec2(0.2073, 0.6483),
    vec2(0.2165, 0.6338),
    vec2(0.2106, 0.6149),
    vec2(0.1964, 0.6084),
    vec2(0.1664, 0.6105),
    vec2(0.1541, 0.5908),
    vec2(0.1709, 0.5676),
    vec2(0.2046, 0.5661),
    vec2(0.2236, 0.5748),
    vec2(0.2446, 0.6007),
    vec2(0.2506, 0.6228),
    vec2(0.2439, 0.6562),
    vec2(0.2195, 0.6812),
    vec2(0.1796, 0.6899),
    vec2(0.1308, 0.6801),
    vec2(0.1036, 0.6615),
    vec2(0.0765, 0.6190),
    vec2(0.0706, 0.5867),
    vec2(0.0750, 0.5553),
    vec2(0.1007, 0.5132),
    vec2(0.1491, 0.4843),
    vec2(0.2181, 0.4819),
    vec2(0.2723, 0.5033),
    vec2(0.3403, 0.5699),
    vec2(0.3633, 0.6303),
    vec2(0.4246, 0.6605),
    vec2(0.4241, 0.6028),
    vec2(0.4409, 0.5625),
    vec2(0.4708, 0.5452),
    vec2(0.5124, 0.5420),
    vec2(0.5565, 0.5559),
    vec2(0.6446, 0.6401),
    vec2(0.7202, 0.6751),
    vec2(0.6831, 0.5888),
    vec2(0.6832, 0.5636),
    vec2(0.6974, 0.5371),
    vec2(0.7253, 0.5299),
    vec2(0.8342, 0.5361),
    vec2(0.9016, 0.5266),
    vec2(0.9741, 0.4968),
    vec2(0.9897, 0.4650),
    vec2(0.9215, 0.4849),
    vec2(0.8832, 0.4837),
    vec2(0.7792, 0.4426),
    vec2(0.7621, 0.4030),
    vec2(0.8399, 0.4466),
    vec2(0.9022, 0.4644),
    vec2(0.9391, 0.4609),
    vec2(0.9871, 0.4367)
);

const vec2 GEEKO_CENTER = vec2(0.4189, 0.5347);

// Eye geometry extracted from official SVG (3 nested subpaths)
const vec2  GEEKO_EYE_CENTER = vec2(0.8896, 0.3815);   // sclera center
const float GEEKO_SCLERA_R   = 0.058;                   // outer white circle
const float GEEKO_IRIS_R     = 0.041;                   // green iris ring inner edge
const vec2  GEEKO_PUPIL_C    = vec2(0.8978, 0.3728);   // pupil center (offset from eye)
const vec2  GEEKO_PUPIL_R    = vec2(0.018, 0.012);     // pupil semi-axes (horiz ellipse)

// Polygon SDF with AABB early-out (winding-number sign rule)
float sdGeeko(vec2 p) {
    // AABB: polygon spans x=[0.007,0.993], y=[0.256,0.744]
    vec2 dLo = vec2(0.007, 0.256) - p;
    vec2 dHi = p - vec2(0.993, 0.744);
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.04) return sqrt(boxDist2);

    float d = dot(p - GEEKO[0], p - GEEKO[0]);
    float s = 1.0;
    for (int i = 0, j = GEEKO_N - 1; i < GEEKO_N; j = i, i++) {
        vec2 e = GEEKO[j] - GEEKO[i];
        vec2 w = p - GEEKO[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= GEEKO[i].y, p.y < GEEKO[j].y, e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }
    return s * sqrt(d);
}

float geekoSDF(vec2 p, float breathe) {
    return sdGeeko(p / breathe) * breathe;
}


// ── Per-instance UV computation ──────────────────────────────

vec2 computeInstanceUV(int idx, int totalCount, vec2 globalUV, float aspect, float time,
                       float logoScale, float bassEnv, float logoPulse,
                       float sizeMin, float sizeMax, float logoSpin, out float instScale) {
    vec2 uv = globalUV;
    uv.x = (uv.x - 0.5) * aspect + 0.5;

    if (totalCount <= 1) {
        vec2 drift = vec2(
            timeSin(0.17) * 0.012 + timeSin(0.31) * 0.006,
            timeCos(0.23) * 0.010 + timeCos(0.13) * 0.005
        );
        uv -= drift;
        float rotAng = timeSin(0.15) * logoSpin;
        vec2 lp = uv - vec2(0.5);
        uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
                   lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);
        float breathe = 1.0 + timeSin(0.8) * 0.015;
        float springT = fract(time * 1.5);
        float spring = 1.0 + bassEnv * 0.1 * exp(-springT * 6.0) * cos(springT * 20.0);
        instScale = logoScale * breathe * spring;
        uv = (uv - 0.5) / instScale + GEEKO_CENTER;
        return uv;
    }

    // Multi-instance: wide roaming Lissajous
    float h1 = hash21(vec2(float(idx) * 7.31, 3.17));
    float h2 = hash21(vec2(float(idx) * 13.71, 7.23));
    float h3 = hash21(vec2(float(idx) * 5.13, 11.37));
    float h4 = hash21(vec2(float(idx) * 9.77, 17.53));

    float roam = 0.35;
    float f1 = 0.07 + float(idx) * 0.023;
    float f2 = 0.05 + float(idx) * 0.019;
    vec2 drift = vec2(
        timeSin(f1, h1 * TAU) * roam + timeSin(f1 * 2.3, h3 * TAU) * roam * 0.3,
        timeCos(f2, h2 * TAU) * roam * 0.9 + timeCos(f2 * 1.7, h4 * TAU) * roam * 0.25
    );
    uv -= drift;

    float rotAng = timeSin(0.1 + float(idx) * 0.027, h4 * TAU) * logoSpin;
    vec2 lp = uv - vec2(0.5);
    uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
               lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);

    instScale = mix(sizeMin, sizeMax, h3) * logoScale;
    float breathe = 1.0 + timeSin(0.6 + float(idx) * 0.13, h1 * TAU) * 0.015;
    float springT = fract(time * 1.5 + h2);
    float spring = 1.0 + bassEnv * 0.1 * exp(-springT * 6.0) * cos(springT * 20.0);
    instScale *= breathe * spring;
    uv = (uv - 0.5) / instScale + GEEKO_CENTER;
    return uv;
}


// ═══════════════════════════════════════════════════════════════
//  ZONE RENDERING
// ═══════════════════════════════════════════════════════════════

vec4 renderSuseZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params,
                    bool isHighlighted, float bass, float mids, float treble,
                    bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth  = max(params.y, 2.0);

    // ── Parameter reads ────────────────────────────────────────
    float speed         = customParams[0].x >= 0.0 ? customParams[0].x : 0.08;
    float flowSpeed     = customParams[0].y >= 0.0 ? customParams[0].y : 0.15;
    float noiseScale    = customParams[0].z >= 0.0 ? customParams[0].z : 3.5;
    float scaleSize     = customParams[0].w >= 0.0 ? customParams[0].w : 6.0;

    float gridScale     = customParams[1].x >= 0.0 ? customParams[1].x : 0.3;
    float iridescence   = customParams[1].y >= 0.0 ? customParams[1].y : 0.6;
    float brightness    = customParams[1].z >= 0.0 ? customParams[1].z : 0.75;
    float turingStr     = customParams[1].w >= 0.0 ? customParams[1].w : 0.3;

    float fillOpacity       = customParams[2].x >= 0.0 ? customParams[2].x : 0.85;
    float borderGlow        = customParams[2].y >= 0.0 ? customParams[2].y : 0.35;
    float edgeFadeStart     = customParams[2].z >= 0.0 ? customParams[2].z : 30.0;
    float borderBrightness  = customParams[2].w >= 0.0 ? customParams[2].w : 1.4;

    float audioReact    = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;
    float particleStr   = customParams[3].y >= 0.0 ? customParams[3].y : 0.4;
    float innerGlowStr  = customParams[3].z >= 0.0 ? customParams[3].z : 0.45;

    float flowDirection = customParams[5].x >= 0.0 ? customParams[5].x : 0.3;
    float logoScale     = customParams[5].y >= 0.0 ? customParams[5].y : 0.45;
    float logoIntensity = customParams[5].z >= 0.0 ? customParams[5].z : 0.8;
    float logoPulse     = customParams[5].w >= 0.0 ? customParams[5].w : 0.8;

    int   logoCount     = clamp(int(customParams[6].x >= 0.0 ? customParams[6].x : 3.0), 1, 8);
    float logoSizeMin   = customParams[6].y >= 0.0 ? customParams[6].y : 0.4;
    float logoSizeMax   = customParams[6].z >= 0.0 ? customParams[6].z : 1.0;

    float logoSpin      = customParams[7].z >= 0.0 ? customParams[7].z : 0.15;
    float idleStr       = customParams[7].w >= 0.0 ? customParams[7].w : 0.6;

    // ── Chameleon biology effects ────────────────────────────────
    float chromatophoreStr = customParams[3].w >= 0.0 ? customParams[3].w : 0.6;
    float tailPulseStr  = customParams[4].w >= 0.0 ? customParams[4].w : 0.7;
    float camoStr       = customParams[6].w >= 0.0 ? customParams[6].w : 0.25;
    float eyeBeamStr    = customParams[7].x >= 0.0 ? customParams[7].x : 0.5;

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;

    vec2 p = fragCoord - center;
    float d = sdRoundedBox(p, halfSize, borderRadius);
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    float time = iTime;

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, SUSE_TEAL);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, SUSE_GREEN);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, SUSE_TURQ);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, SUSE_GLOW);

    float vitality = isHighlighted ? 1.0 : 0.3;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * timeSin(0.8 * PI)) * idleStr;

    float flowAngle = flowDirection * TAU;
    vec2 flowDir = vec2(cos(flowAngle), sin(flowAngle));

    // ── Audio envelopes ────────────────────────────────────────
    float bassEnv   = hasAudio ? smoothstep(0.01, 0.18, bass) * audioReact : idlePulse;
    float midsEnv   = hasAudio ? smoothstep(0.01, 0.25, mids) * audioReact : idlePulse * 0.6;
    float trebleEnv = hasAudio ? smoothstep(0.02, 0.30, treble) * audioReact : 0.0;

    // ── Background UV ─────────────────────────────────────────
    vec2 centeredUV = (globalUV * 2.0 - 1.0) * noiseScale;
    centeredUV.x *= aspect;

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        // ═══════════════════════════════════════════════════════
        //  LAYER 0: SUB-SURFACE ENERGY VEINS (beneath scales)
        // ═══════════════════════════════════════════════════════
        // Domain-warped FBM veins visible through gaps between scales.
        // Bass makes veins slither and brighten, mids shift their color.

        vec2 veinUV = centeredUV * 1.3 + flowDir * time * flowSpeed * 0.5;
        float warpStr = 0.08 + bassEnv * 0.2 + midsEnv * 0.1;
        float wn1 = noise2D(veinUV * 1.8 + time * 0.6);
        float wn2 = noise2D(veinUV * 1.8 + time * 0.6 + vec2(97.0, 53.0));
        veinUV += (vec2(wn1, wn2) - 0.5) * warpStr;

        float veinNoise = fbm(veinUV, 5, 0.7);
        float veinWidth = 0.03 + bassEnv * 0.02;
        float veins = smoothstep(veinWidth, 0.0, abs(veinNoise - 0.5)) * 0.3;
        float veinPulse = 1.0 + bassEnv * 0.4 * sin(veinNoise * 12.0 - time * 4.0);

        // Vein base color (deep green energy flowing under skin)
        vec3 veinCol = susePalette(veinNoise + midsEnv * 0.15 + time * 0.03,
                                    palAccent, palGlow, palSecondary);

        // ═══════════════════════════════════════════════════════
        //  LAYER 1: IRIDESCENT REPTILE SCALE FIELD
        // ═══════════════════════════════════════════════════════

        vec2 scaleUV = centeredUV + flowDir * time * flowSpeed * 0.3;
        ScaleCell cell = hexScaleGrid(scaleUV, scaleSize);

        // Bass makes scales breathe (radius pulsation)
        float breatheAmount = 1.0 + bassEnv * 0.2 * sin(cell.cellHash * TAU + time * 2.0);
        float scaledEdge = cell.edgeDist * breatheAmount;

        // Scale rim lighting (raised reptile skin look)
        float scaleRim = smoothstep(0.0, 0.06, scaledEdge);
        float scaleHighlight = pow(scaleRim, 0.4);

        // Thin-film iridescence per scale
        float cosTheta = clamp(1.0 - length(cell.local) * 3.0, 0.0, 1.0);
        float filmThickness = 0.4 + 0.3 * cell.cellHash
                            + midsEnv * 0.3 * sin(cell.cellHash * 12.0 + time * 1.5);
        vec3 iriCol = thinFilm(cosTheta, filmThickness);

        // Blend iridescence with brand palette
        float colorT = cell.cellHash + time * speed * 0.5;
        vec3 baseScaleCol = susePalette(colorT, palPrimary, palSecondary, palAccent);
        vec3 scaleCol = mix(baseScaleCol, iriCol * palSecondary * 2.0, iridescence);

        // Scale gap color — veins visible through gaps between scales
        vec3 gapCol = palPrimary * 0.3 + veinCol * veins * veinPulse;
        float rimStrength = mix(0.3, 1.0, gridScale / 0.6);
        vec3 col = mix(gapCol, scaleCol * scaleHighlight * brightness, scaleRim * rimStrength);

        // ── Per-cell interior shapes (living scales) ──────────
        if (scaleRim > 0.05) {
            // Domain-warped energy inside each cell
            vec2 cellUV = cell.local * 6.0 + cell.id * 3.7;
            float cellEnergy = fbm(cellUV + time * speed * 2.0, 4, 0.7);
            float cellR = fbm(cellUV + cellEnergy * 1.2 + time * speed, 4, 0.7);

            // Per-cell concentric ring pattern (nanocrystal structure)
            float cellDist = length(cell.local) * 4.0;
            float rings = sin(cellDist * 8.0 - time * 1.5 - cell.cellHash * TAU) * 0.5 + 0.5;
            rings *= smoothstep(0.5, 0.15, cellDist / 4.0); // fade toward edges

            // Blend energy + rings, colored by palette position
            vec3 cellInner = susePalette(cellR + cell.cellHash + time * 0.03,
                                          palSecondary, palAccent, palGlow);
            col = mix(col, cellInner * brightness * 1.2, rings * 0.2 * scaleRim);

            // Bass pumps individual cells (wave travels across grid)
            float bassWave = sin(cell.id.x * 1.5 + cell.id.y * 0.8 - time * 3.0);
            float bassCellPump = bassEnv * 0.25 * smoothstep(-0.2, 0.5, bassWave);
            col *= 1.0 + bassCellPump;

            // Mids shift cell color temperature
            float midsWave = sin(cell.id.x * 0.7 + cell.id.y * 1.3 + time * 0.5);
            vec3 warmShift = susePalette(cell.cellHash + 0.33 + midsWave * 0.15,
                                          palGlow, palSecondary, palAccent);
            col = mix(col, warmShift * brightness, midsEnv * 0.15);
        }

        // ── Scale edge energy veins ───────────────────────────
        if (scaledEdge < 0.04 && scaledEdge > 0.0) {
            float crease = smoothstep(0.03, 0.003, scaledEdge);
            float edgeFlow = noise2D(cell.id * 15.0 + flowDir * time * 2.5);
            float flowPulse = smoothstep(0.3, 0.7, edgeFlow);
            vec3 creaseCol = mix(palGlow, palAccent, flowPulse);
            col += creaseCol * crease * (0.15 + bassEnv * 0.2);
        }

        // ── Scale ripple propagation: bass color-change wave ─────
        // Real chameleons propagate color changes as visible waves
        // across their skin — here the hex field acts as extended skin.
        {
            float ripplePhase = fract(time * 0.25);
            float rippleRadius = ripplePhase * noiseScale * 1.5;
            float rippleDist = length(centeredUV);
            // Gaussian ring wavefront
            float rippleRing = exp(-pow((rippleDist - rippleRadius) * 4.0, 2.0));
            float rippleFade = 1.0 - ripplePhase;
            float rippleAudio = bassEnv * 0.35 * rippleFade;
            // Color shifts through complementary hues as wave passes
            vec3 rippleShift = susePalette(cell.cellHash + ripplePhase * 2.0,
                                            palAccent, palGlow, palSecondary);
            col = mix(col, rippleShift * brightness * 1.5,
                      rippleRing * rippleAudio * scaleRim);
            // Trail: cells already swept retain a brief afterglow
            float rippleTrail = smoothstep(rippleRadius, rippleRadius - 0.3, rippleDist)
                              * rippleAudio * 0.2;
            col += palGlow * rippleTrail * scaleRim * 0.15;
        }

        // ── Treble sparkle on scale edges ─────────────────────
        if (trebleEnv > 0.01 && scaledEdge < 0.08 && scaledEdge > 0.01) {
            float sparkN = noise2D(cell.id * 50.0 + time * 8.0);
            float spark = smoothstep(0.55, 0.95, sparkN) * trebleEnv;
            col += palGlow * spark * 0.6;
            // Bright point sparks (like light catching crystal facets)
            float pointSpark = step(0.92, sparkN) * trebleEnv * 2.0;
            col += vec3(1.0) * pointSpark * 0.3;
        }

        // ── Nanocrystal prism bursts ─────────────────────────
        // Chameleons shift color via nanocrystal lattice spacing in
        // their skin. On treble hits, individual scales "fire" —
        // their crystal lattice diffracts light into rainbow bursts.
        if (trebleEnv > 0.05 && scaledEdge < 0.06) {
            float prismSeed = noise2D(cell.id * 30.0 + time * 5.0);
            if (prismSeed > 0.65) {
                float prismPhase = fract(prismSeed * 7.0 + time * 3.0);
                // Rapid thin-film cycling = prismatic rainbow
                vec3 prismCol = thinFilm(prismPhase, 0.25 + prismPhase * 0.6);
                float prismStr = (prismSeed - 0.65) * 2.86 * trebleEnv;
                // Burst radiates from cell center
                float prismDist = length(cell.local) * 5.0;
                float prismBurst = exp(-prismDist * prismDist * 2.0);
                col += prismCol * prismStr * prismBurst * 0.5;
            }
        }

        // ═══════════════════════════════════════════════════════
        //  LAYER 2: TURING PATTERN OVERLAY (chameleon markings)
        // ═══════════════════════════════════════════════════════

        if (turingStr > 0.01) {
            float turingTime = time * speed * 2.0 + midsEnv * 0.5;
            float turing = turingPattern(centeredUV * 0.8, turingTime);
            vec3 markingCol = mix(palPrimary * 0.5, palSecondary * 0.8, turing);
            col = mix(col, markingCol, turingStr * turing * 0.5);
        }

        // ═══════════════════════════════════════════════════════
        //  LAYER 3: CURL NOISE DUST PARTICLES (tumbleweed wind)
        // ═══════════════════════════════════════════════════════

        if (particleStr > 0.01) {
            vec3 dustCol = vec3(0.0);
            float windSpeed = 1.0 + bassEnv * 2.0;

            for (int pi = 0; pi < 25; pi++) {
                float fi = float(pi);
                vec2 seed = vec2(hash11(fi * 7.3), hash11(fi * 11.1 + 5.0));

                vec2 pos = seed;
                for (int step = 0; step < 3; step++) {
                    pos += curlNoise(pos * 3.0, time * windSpeed + fi) * 0.015;
                }
                pos = fract(pos);

                vec2 diff = globalUV - pos;
                diff.x *= aspect;
                float dist = length(diff);
                float particle = exp(-dist * dist * 3000.0);
                float twinkle = 0.5 + 0.5 * sin(time * 3.0 + fi * 2.3);
                float burst = 1.0 + trebleEnv * 0.6 * step(0.85, hash11(fi * 3.1 + floor(time * 0.7)));

                dustCol += mix(palGlow, palAccent, hash11(fi * 5.7)) * particle * twinkle * burst;
            }
            col += dustCol * particleStr * 0.3;
        }

        // ═══════════════════════════════════════════════════════
        //  LAYER 4: GEEKO LOGO (multi-instance polygon SDF)
        // ═══════════════════════════════════════════════════════

        for (int li = 0; li < logoCount && li < 8; li++) {
            float instScale;
            vec2 iLogoUV = computeInstanceUV(li, logoCount, globalUV, aspect, time,
                                              logoScale, bassEnv, logoPulse,
                                              logoSizeMin, logoSizeMax, logoSpin, instScale);

            // Bounding check
            if (iLogoUV.x < -0.2 || iLogoUV.x > 1.2 ||
                iLogoUV.y < 0.0 || iLogoUV.y > 1.0) continue;

            float maxScale = logoSizeMax * logoScale;
            float depthFactor = clamp(instScale / max(maxScale, 0.01), 0.0, 1.0);
            float instIntensity = logoIntensity * (0.3 + 0.7 * depthFactor);

            // Breathe scaling for bass pulse
            float breathe = 1.0 + bassEnv * logoPulse * 0.04;
            float fDist = geekoSDF(iLogoUV, breathe);

            if (fDist > 0.12) continue;

            // ── Outer effects ─────────────────────────────────
            vec3 outerCol = vec3(0.0);

            // Soft iridescent edge glow
            if (fDist > -0.005 && fDist < 0.06) {
                float auraDist = max(fDist, 0.0);
                float aura = exp(-auraDist * 30.0) * 0.3;
                float auraAngle = atan(iLogoUV.y - GEEKO_CENTER.y,
                                       iLogoUV.x - GEEKO_CENTER.x);
                float auraThickness = 0.5 + 0.2 * sin(auraAngle * 3.0 + time * 0.8);
                vec3 auraIri = thinFilm(0.5 + 0.2 * sin(auraAngle * 2.0),
                                         auraThickness + midsEnv * 0.15);
                outerCol += auraIri * palGlow * aura;
            }

            // Treble edge discharge (sparks jumping off logo boundary)
            if (trebleEnv > 0.01 && fDist > -0.005 && fDist < 0.025) {
                float sparkN = noise2D(iLogoUV * 35.0 + time * 7.0 + float(li) * 33.0);
                sparkN = smoothstep(0.5, 0.95, sparkN);
                float edgeMask = smoothstep(0.025, 0.0, abs(fDist));
                outerCol += mix(palGlow, vec3(1.0), 0.5) * sparkN * edgeMask * trebleEnv * 1.5 * depthFactor;
            }

            // ── Logo fill ──────────────────────────────────────
            vec3 logoCol = vec3(0.0);
            float fillAlpha = 0.0;

            if (fDist < 0.005) {
                if (fDist < 0.0) {
                    // ── Chameleon color-shift fill ─────────────────
                    vec2 fillLP = iLogoUV - GEEKO_CENTER;
                    float fillAngle = atan(fillLP.y, fillLP.x);
                    float fillR = length(fillLP);

                    // Body regions: head is warmer, tail cooler
                    float headRegion = smoothstep(0.3, 0.15, length(iLogoUV - vec2(0.85, 0.35)));
                    float tailRegion = smoothstep(0.25, 0.1, length(iLogoUV - vec2(0.15, 0.55)));

                    // Base green + color-shift wave head-to-tail
                    float bodyAxis = dot(fillLP, normalize(vec2(1.0, -0.2)));
                    float shiftWave = bodyAxis * 3.0 + time * speed * 4.0;
                    float shiftPhase = sin(shiftWave) * 0.5 + 0.5;

                    vec3 shiftTarget = susePalette(shiftPhase + midsEnv * 0.15,
                                                    palSecondary, palAccent, palGlow);
                    float angularShift = sin(fillAngle * 2.0 + time * 0.3) * 0.06;
                    shiftTarget = mix(shiftTarget, susePalette(shiftPhase + angularShift + 0.33,
                                                               palAccent, palGlow, palSecondary), 0.2);
                    // Start from a saturated, boosted green (not raw palette)
                    vec3 vivid = palSecondary * 1.6 + palGlow * 0.4;
                    vec3 bodyCol = mix(vivid, shiftTarget * 1.4, 0.55);
                    logoCol = bodyCol * brightness * 2.5;

                    // ── Chromatophore cascade: bass color-change wavefront ──
                    // Real chameleons shift color via nanocrystal spacing
                    // changes that propagate as a visible wave across skin.
                    if (chromatophoreStr > 0.01 && bassEnv > 0.05) {
                        float chromaPhase = fract(time * 0.35 + float(li) * 0.17);
                        float chromaRadius = chromaPhase * 0.5;
                        float chromaDist = length(fillLP);
                        // Gaussian ring wavefront expanding outward from center
                        float chromaRing = exp(-pow((chromaDist - chromaRadius) * 15.0, 2.0));
                        float chromaFade = 1.0 - chromaPhase;
                        float chromaAudio = bassEnv * chromatophoreStr * chromaFade;
                        // Target hue: complementary shift (green→turquoise→teal cycle)
                        vec3 chromaTarget = susePalette(
                            chromaDist * 4.0 + chromaPhase * 2.0,
                            palAccent, palGlow, palSecondary);
                        logoCol = mix(logoCol, chromaTarget * brightness * 2.2,
                                      chromaRing * chromaAudio);
                        // Trail: swept area retains shifted hue briefly
                        float chromaTrail = smoothstep(chromaRadius,
                                                        chromaRadius - 0.12,
                                                        chromaDist)
                                          * chromaAudio * 0.25;
                        logoCol = mix(logoCol, chromaTarget * brightness * 1.5,
                                      chromaTrail);
                    }

                    // ── Sub-skin energy (domain-warped FBM under scales) ──
                    vec2 energyUV = iLogoUV * 4.0 + flowDir * time * flowSpeed * 1.5;
                    float eq = fbm(energyUV + time * speed * 1.2, 5, 0.7);
                    float er = fbm(energyUV + eq * 1.5 + time * speed, 5, 0.7);
                    vec3 energyCol = susePalette(er * 1.2 + time * 0.05 + float(li) * 0.3,
                                                  palSecondary, palGlow, palAccent);
                    // Blend energy under the color shift (visible as flowing depth)
                    logoCol = mix(logoCol, energyCol * brightness * 2.4, 0.35);

                    // ── Bass reactive brightness wave across body ─────
                    float bassBodyWave = sin(bodyAxis * 6.0 - time * 4.0);
                    logoCol *= 1.0 + bassEnv * logoPulse * 0.3 * smoothstep(-0.3, 0.5, bassBodyWave);

                    // Regional tinting (vibrant)
                    logoCol = mix(logoCol, palGlow * brightness * 2.8, headRegion * 0.3);
                    logoCol = mix(logoCol, palAccent * brightness * 2.5, tailRegion * 0.25);

                    // ── Fresnel-like interior edge highlight ───────────
                    float fresnelLike = smoothstep(-0.02, -0.002, fDist);
                    logoCol += palGlow * fresnelLike * 0.35 * (1.0 + midsEnv * 0.5);

                    // ── Turret eye: independent orbit + iridescent iris ──
                    // Chameleon eyes rotate independently on turrets and
                    // are one of the most iconic features of the animal.
                    float eyeDist = length(iLogoUV - GEEKO_EYE_CENTER);
                    if (eyeDist < GEEKO_SCLERA_R * 1.5) {
                        float scleraEdge = smoothstep(GEEKO_SCLERA_R,
                                                       GEEKO_SCLERA_R - 0.004, eyeDist);
                        vec3 scleraCol = vec3(0.95, 0.93, 0.88);

                        // Iridescent iris ring — mids make the nanocrystals shift
                        float irisOuter = smoothstep(GEEKO_SCLERA_R,
                                                      GEEKO_SCLERA_R - 0.004, eyeDist);
                        float irisInner = smoothstep(GEEKO_IRIS_R - 0.003,
                                                      GEEKO_IRIS_R, eyeDist);
                        float irisMask = irisOuter * irisInner;
                        float irisFilmT = 0.4 + midsEnv * 0.4 + time * 0.3;
                        vec3 irisIri = thinFilm(0.6 + 0.2 * sin(time * 0.5),
                                                 irisFilmT);
                        vec3 irisCol = mix(palSecondary,
                                           irisIri * palGlow,
                                           0.35 + midsEnv * 0.35) * 1.6;
                        scleraCol = mix(scleraCol, irisCol, irisMask);

                        // Turret pupil: slow independent orbit
                        float eyeOrbitAngle = time * 0.4 + sin(time * 0.7) * 0.8;
                        vec2 pupilOrbit = vec2(cos(eyeOrbitAngle),
                                               sin(eyeOrbitAngle)) * 0.005;
                        vec2 pupilCenter = GEEKO_PUPIL_C + pupilOrbit;
                        // Pupil dilates on bass (chameleons dilate when excited)
                        float pupilDilate = 1.0 + bassEnv * 0.4;
                        vec2 pupilLP = (iLogoUV - pupilCenter)
                                     / (GEEKO_PUPIL_R * pupilDilate);
                        float pupilDist = length(pupilLP);
                        float pupilMask = smoothstep(1.1, 0.7, pupilDist);
                        scleraCol = mix(scleraCol, vec3(0.03, 0.05, 0.02),
                                        pupilMask);

                        // Specular highlight
                        vec2 specOff = GEEKO_EYE_CENTER + vec2(-0.008, 0.006);
                        float specDist = length(iLogoUV - specOff);
                        float spec = smoothstep(0.008, 0.003, specDist) * 0.4;
                        scleraCol += vec3(spec);

                        // Attention cone: glow beam in look direction
                        if (eyeBeamStr > 0.01) {
                            float beamAngle = atan(
                                iLogoUV.y - GEEKO_EYE_CENTER.y,
                                iLogoUV.x - GEEKO_EYE_CENTER.x);
                            float beamSpread = abs(
                                mod(beamAngle - eyeOrbitAngle + PI, TAU) - PI);
                            float beamR = eyeDist;
                            float cone = smoothstep(0.3, 0.0, beamSpread)
                                       * smoothstep(0.0, 0.008,
                                                     beamR - GEEKO_SCLERA_R)
                                       * exp(-(beamR - GEEKO_SCLERA_R) * 10.0);
                            scleraCol += mix(palGlow, vec3(1.0), 0.3)
                                       * cone * eyeBeamStr
                                       * (0.5 + midsEnv * 0.5);
                        }

                        logoCol = mix(logoCol, scleraCol, scleraEdge);
                    }

                    // Reinhard tonemap on interior only
                    logoCol = logoCol / (1.0 + logoCol);

                    // ── Camouflage shimmer: stealth fade ─────────────
                    // Chameleons blend with their environment. Parts of
                    // the body fade toward the background, then snap
                    // back to full visibility on audio hits.
                    if (camoStr > 0.01) {
                        float camoPattern = turingPattern(iLogoUV * 2.0,
                                                           time * 0.5);
                        float camoWave = sin(time * 0.3 + float(li) * 1.7)
                                       * 0.5 + 0.5;
                        // Bass startles the chameleon → full visibility
                        float camoSuppress = 1.0 - bassEnv * 0.8;
                        float camoFade = camoPattern * camoWave * camoStr
                                       * camoSuppress;
                        // Blend logo toward background color (subtle)
                        logoCol = mix(logoCol, col * 0.7, camoFade * 0.15);
                    }

                    // ── Saturation boost: make logo pop against background ──
                    float logoLum = luminance(logoCol);
                    logoCol = mix(vec3(logoLum), logoCol, 1.6);
                    logoCol = max(logoCol, vec3(0.0));

                    fillAlpha = smoothstep(0.005, -0.005, fDist);
                }

                // ── Multi-layer glow ──────────────────────────────
                float glow1 = exp(-max(fDist, 0.0) * 80.0) * 0.45;
                float glow2 = exp(-max(fDist, 0.0) * 25.0) * 0.2;
                float glow3 = exp(-max(fDist, 0.0) * 8.0) * 0.1;
                vec3 edgeCol = susePalette(time * 0.06 + iLogoUV.y + float(li) * 0.2,
                                            palGlow, palSecondary, palAccent);
                float flare = 1.0 + bassEnv * 0.4;
                outerCol += edgeCol * glow1 * flare * depthFactor;
                outerCol += palSecondary * glow2 * flare * 0.5 * depthFactor;
                outerCol += palAccent * glow3 * 0.3 * depthFactor;
            }

            // ── Energy veins along logo boundary ──────────────
            if (fDist < 0.0 && fDist > -0.02) {
                float boundaryDist = -fDist;
                float crease = smoothstep(0.015, 0.002, boundaryDist);
                float edgeFlow = noise2D(iLogoUV * 30.0 + flowDir * time * 2.0 + float(li) * 50.0);
                float flowPulse = smoothstep(0.3, 0.7, edgeFlow);
                vec3 creaseCol = mix(palGlow, vec3(1.0), 0.25);
                col = mix(col, col + creaseCol * crease * (0.2 + flowPulse * 0.25) * depthFactor, fillAlpha);

                // Treble sparks along edge veins
                if (trebleEnv > 0.01) {
                    float sparkN = noise2D(iLogoUV * 50.0 + time * 8.0 + float(li) * 77.0);
                    float spark = step(0.92, sparkN) * trebleEnv * 1.5;
                    col += vec3(1.0) * crease * spark * depthFactor * fillAlpha;
                }
            }

            // ── Scanning beam (slow sweep across logo) ────────
            {
                float scanSpeed = 0.2 + trebleEnv * 0.15;
                float scanPos = fract(time * scanSpeed * 0.1 + float(li) * 0.23);
                float beamDist = abs(iLogoUV.y - scanPos);
                float beam = smoothstep(0.02, 0.0, beamDist);
                float beamWide = smoothstep(0.06, 0.0, beamDist) * 0.15;
                float beamMask = smoothstep(0.02, -0.003, fDist);
                vec3 beamCol = mix(palSecondary, vec3(1.0), 0.3);
                col += beamCol * (beam + beamWide) * beamMask * instIntensity * 0.5 * depthFactor;
            }

            // ── Tail pulse: bass-triggered energy spiral ─────
            // Energy wave propagates from body into the curled tail,
            // spiraling through the prehensile curl and sparking at tip.
            if (tailPulseStr > 0.01) {
                vec2 tailCenter = vec2(0.17, 0.60);   // Geeko tail spiral center
                vec2 tailBase   = vec2(0.34, 0.57);   // where body meets tail
                vec2 tailTip    = vec2(0.19, 0.74);   // outermost curl point

                float tailPhase = fract(time * 0.45 + float(li) * 0.23);
                float tailTrigger = smoothstep(0.08, 0.22, bassEnv)
                                  * tailPulseStr;

                if (tailTrigger > 0.01) {
                    // Distance along tail spiral (body→curl center→tip)
                    vec2 toPixel = iLogoUV - tailCenter;
                    float spiralAngle = atan(toPixel.y, toPixel.x);
                    float spiralR = length(toPixel);

                    // Parametric progress: body→center→tip as 0→1
                    float bodyDist = length(iLogoUV - tailBase);
                    float tipDist  = length(iLogoUV - tailTip);
                    float tailProgress = clamp(bodyDist / (bodyDist + tipDist + 0.001), 0.0, 1.0);

                    // Traveling wave front from body toward tip
                    float waveFront = tailPhase * 1.4;
                    float wave = exp(-pow((tailProgress - waveFront) * 8.0, 2.0));
                    // Trail: already-passed region retains fading glow
                    float trail = smoothstep(waveFront, waveFront - 0.35, tailProgress)
                                * (1.0 - tailPhase) * 0.4;

                    // Only affect pixels near the tail region (mask to tail SDF)
                    float tailMask = smoothstep(0.15, 0.04, spiralR);

                    // Spiral energy lines rotating through the curl
                    float spiralLines = sin(spiralAngle * 3.0 - time * 2.5
                                          + spiralR * 18.0) * 0.5 + 0.5;
                    spiralLines = pow(spiralLines, 3.0);

                    float intensity = (wave + trail) * tailTrigger * tailMask;

                    // Color: green at base, turquoise through curl, bright glow at tip
                    vec3 tailCol = mix(palSecondary * 2.2, palAccent * 2.5,
                                       tailProgress);
                    tailCol = mix(tailCol, palGlow * 3.0,
                                  smoothstep(0.7, 1.0, tailProgress));
                    tailCol *= brightness * (1.2 + bassEnv * 0.6);

                    // Spiral energy overlay
                    col += tailCol * intensity * depthFactor;
                    col += palAccent * spiralLines * intensity * 0.3 * depthFactor;

                    // Tip spark: bright flash at the curl tip
                    float tipSpark = smoothstep(0.04, 0.005, tipDist);
                    float sparkPulse = smoothstep(0.8, 1.0, waveFront)
                                     * (1.0 - tailPhase);
                    col += palGlow * 2.5 * tipSpark * sparkPulse * tailTrigger * depthFactor;

                    // Glow halo around tail spiral
                    float tailGlow = exp(-spiralR * 12.0) * 0.15 * intensity;
                    col += palGlow * tailGlow * depthFactor;
                }
            }

            // ── Compositing: interior opaque, outer additive ──
            col = mix(col, logoCol, fillAlpha);

            float logoVignette = smoothstep(0.4, 0.0, length(iLogoUV - GEEKO_CENTER));
            float outerMask = (1.0 - fillAlpha) * logoVignette;
            col += outerCol * outerMask * instIntensity;

        } // end logo instance loop

        // ── Vitality ───────────────────────────────────────────
        col *= mix(0.85, 1.1, vitality);
        if (!isHighlighted) {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.08);
            col += col * idlePulse * 0.08;
        }

        // ── Inner edge glow (iridescent scale rim) ────────────
        float innerDist = -d;
        float depthDarken = smoothstep(0.0, edgeFadeStart, innerDist);
        col *= mix(0.8, 1.0, 1.0 - depthDarken * 0.2);

        float innerGlow = exp(-innerDist / 12.0);
        float edgeAngle = atan(p.y, p.x);
        float iriT = edgeAngle / TAU + time * 0.05 + midsEnv * 0.2;
        vec3 edgeIriCol = susePalette(iriT, palSecondary, palAccent, palGlow);
        col += edgeIriCol * innerGlow * innerGlowStr;

        col = mix(col, fillColor.rgb * luminance(col), 0.07);

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // ── Border ───────────────────────────────────────────────
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float angle = atan(p.y, p.x) * 2.0;
        ScaleCell borderCell = hexScaleGrid(vec2(angle, d * 0.1), 4.0);
        float borderPattern = smoothstep(0.0, 0.04, borderCell.edgeDist);
        vec3 borderFilm = thinFilm(borderCell.cellHash, 0.4 + time * 0.08 + midsEnv * 0.15);

        vec3 borderCol = susePalette(borderCell.cellHash + time * 0.05,
                                       palSecondary, palAccent, palGlow);
        borderCol = mix(borderCol, borderFilm * palSecondary * 2.0, 0.3 * iridescence);
        borderCol *= borderPattern * borderBrightness;

        vec3 zoneBorderTint = colorWithFallback(borderColor.rgb, borderCol);
        borderCol = mix(borderCol, zoneBorderTint * luminance(borderCol), 0.25);

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

    // ── Outer glow ───────────────────────────────────────────
    float bassGlowPush = hasAudio ? bassEnv * 2.0 : idlePulse * 5.0;
    float glowRadius = mix(10.0, 18.0, vitality) + bassGlowPush;
    if (d > 0.0 && d < glowRadius && borderGlow > 0.01) {
        float glow = expGlow(d, 7.0, borderGlow);
        float angle = atan(p.y, p.x);
        float glowT = angularNoise(angle, 1.5, time * 0.08) + midsEnv * 0.15;
        vec3 glowCol = susePalette(glowT, palSecondary, palAccent, palGlow);
        glowCol *= mix(0.3, 1.0, vitality);
        result.rgb += glowCol * glow * 0.5;
        result.a = max(result.a, glow * 0.4);
    }

    return result;
}


// ─── Custom Label Composite (iridescent scale-reveal text) ───────

vec4 compositeSuseLabels(vec4 color, vec2 fragCoord,
                         float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, SUSE_TEAL);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, SUSE_GREEN);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, SUSE_TURQ);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, SUSE_GLOW);

    float labelGlowSpread = customParams[4].x >= 0.0 ? customParams[4].x : 3.0;
    float labelBrightness = customParams[4].y >= 0.0 ? customParams[4].y : 2.5;
    float labelAudioReact = customParams[4].z >= 0.0 ? customParams[4].z : 1.0;
    float iridescence     = customParams[1].y >= 0.0 ? customParams[1].y : 0.6;

    float bassR = hasAudio ? bass * labelAudioReact : 0.0;
    float midsR = hasAudio ? mids * labelAudioReact : 0.0;
    float trebleR = hasAudio ? treble * labelAudioReact : 0.0;

    // ── Expanded halo (two-layer: inner sharp + outer soft) ──────
    float haloInner = 0.0;
    float haloOuter = 0.0;
    for (int i = 0; i < 16; i++) {
        float angle = float(i) * TAU / 16.0;
        for (int r = 1; r <= 4; r++) {
            float radius = float(r) * labelGlowSpread;
            vec2 offset = vec2(cos(angle), sin(angle)) * px * radius;
            float s = texture(uZoneLabels, uv + offset).a;
            if (r <= 2) haloInner += s * exp(-float(r) * 0.4);
            haloOuter += s * exp(-float(r) * 0.35);
        }
    }
    haloInner /= 24.0;
    haloOuter /= 48.0;

    if (haloOuter > 0.002) {
        float haloMask = haloOuter * (1.0 - labels.a);
        float innerMask = haloInner * (1.0 - labels.a);

        // Color-shifting halo — chromatophore wave sweeps through
        float haloAngle = atan(uv.y - 0.5, uv.x - 0.5);
        float chromaWave = sin(haloAngle * 2.0 + iTime * 1.2 + midsR * 1.5)
                         * 0.5 + 0.5;

        vec3 haloCol1 = susePalette(chromaWave + iTime * 0.08,
                                     palGlow, palAccent, palSecondary);
        vec3 haloFilm = thinFilm(0.5 + 0.3 * sin(haloAngle * 3.0 + iTime * 0.5),
                                  0.6 + midsR * 0.3);
        vec3 haloCol = mix(haloCol1 * 1.4, haloFilm * palGlow * 2.0,
                           iridescence * 0.6);

        // Inner halo: bright, saturated ring hugging the text
        float innerBright = innerMask * (0.8 + bassR * 0.6);
        color.rgb += haloCol * innerBright * 1.2;

        // Outer halo: softer, wider atmospheric glow
        float outerBright = haloMask * (0.3 + bassR * 0.2);
        color.rgb += mix(haloCol, palAccent, 0.3) * outerBright * 0.6;

        // Treble edge sparks — electrical arcs along the halo boundary
        if (trebleR > 0.08) {
            float sparkN = noise2D(uv * 50.0 + iTime * 5.0);
            float spark = smoothstep(0.65, 0.92, sparkN) * trebleR * 2.5;
            // Sparks travel around the halo perimeter
            float arcPos = fract(haloAngle / TAU + iTime * 0.8);
            float arc = smoothstep(0.0, 0.08, arcPos) * smoothstep(0.25, 0.12, arcPos);
            color.rgb += vec3(1.0, 0.95, 0.8) * innerMask * spark * (0.6 + arc * 0.8);
        }
        color.a = max(color.a, innerMask * 0.7);
    }

    // ── Text fill: iridescent scale-patterned interior ──────────
    if (labels.a > 0.01) {
        // Reuse the proper hex grid — animate UV so scales drift
        vec2 textScaleUV = (uv * 2.0 - 1.0) * 12.0;
        textScaleUV.x *= iResolution.x / max(iResolution.y, 1.0);
        textScaleUV += vec2(iTime * 0.15, iTime * 0.08);  // slow drift
        ScaleCell cell = hexScaleGrid(textScaleUV, 4.0);

        // Bass makes scales breathe (radius pulsation per cell)
        float breatheAmount = 1.0 + bassR * 0.25
                            * sin(cell.cellHash * TAU + iTime * 2.5);
        float scaledEdge = cell.edgeDist * breatheAmount;
        float scaleRim = smoothstep(0.0, 0.05, scaledEdge);
        float scaleHighlight = pow(scaleRim, 0.5);

        // Thin-film iridescence per cell — thickness cycles with time
        float cosT = clamp(1.0 - length(cell.local) * 3.0, 0.0, 1.0);
        float thickness = 0.4 + 0.3 * cell.cellHash + midsR * 0.3
                        + sin(iTime * 0.6 + cell.cellHash * TAU) * 0.15;
        vec3 cellFilm = thinFilm(cosT, thickness);

        // Color-shift wave sweeping diagonally across text
        float textWave = sin(uv.x * 8.0 - iTime * 1.8 + uv.y * 4.0) * 0.5 + 0.5;
        // Per-cell color cycles through palette over time
        vec3 textPalette = susePalette(cell.cellHash + textWave * 0.5
                                        + iTime * 0.1,
                                        palGlow, palSecondary, palAccent);

        // Palette tinted by iridescent film
        vec3 textCol = textPalette * mix(vec3(1.0), cellFilm, iridescence * 0.6);
        // Scale structure: bright centers, visible gaps with energy
        vec3 gapCol = palPrimary * 0.2 + palSecondary * 0.08
                    + palAccent * 0.06 * sin(iTime * 2.0 + cell.cellHash * 10.0);
        textCol = mix(gapCol, textCol * scaleHighlight, scaleRim);

        // Edge glint — flows along scale edges
        if (scaledEdge < 0.04 && scaledEdge > 0.0) {
            float edgeFlow = noise2D(cell.id * 15.0 + iTime * 2.0);
            float flowPulse = smoothstep(0.3, 0.7, edgeFlow);
            float edgeGlint = smoothstep(0.03, 0.005, scaledEdge);
            vec3 glintCol = mix(palGlow, palAccent, flowPulse);
            textCol += glintCol * edgeGlint * (0.1 + trebleR * 0.15);
        }

        // Bass: overall brightness pulse
        float breathe = 1.0 + bassR * 0.4;
        textCol *= breathe;

        // Apply brightness then tonemap to preserve saturation
        textCol *= labelBrightness;
        textCol = textCol / (0.6 + textCol);

        // Saturation boost post-tonemap
        float textLum = dot(textCol, vec3(0.2126, 0.7152, 0.0722));
        textCol = mix(vec3(textLum), textCol, 1.5);
        textCol = max(textCol, vec3(0.0));

        color.rgb = mix(color.rgb, textCol, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}


void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderSuseZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    // showLabels guard: default ON (unset = -1, which is < 0)
    float showLabelsVal = customParams[7].y;
    if (showLabelsVal < 0.0 || showLabelsVal > 0.5)
        color = compositeSuseLabels(color, fragCoord, bass, mids, treble, hasAudio);

    fragColor = clampFragColor(color);
}
