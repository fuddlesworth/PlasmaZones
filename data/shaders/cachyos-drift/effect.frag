// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * CACHYOS DRIFT - Fragment Shader (Procedural Logo v3 — Multi-Instance)
 *
 * Multiple CachyOS logos drawn as SDF geometry, each drifting independently
 * through the scene with unique sizes, trajectories, and phase offsets.
 * Rich domain-warped FBM background, per-facet audio reactivity,
 * bass shockwaves, and organic motion.
 *
 * Logo geometry: 12 vertices, 7 teal facets + cyan polygon body, 3 circles.
 * Cyan body uses a proper polygon SDF for the concave "C" silhouette.
 * Coordinates normalized 0-1 from the official SVG.
 *
 * Audio reactivity (organic — modulates existing animation, no UV warping):
 *   Bass  = flow acceleration + vein glow + shockwave rings + facet scatter
 *   Mids  = palette warmth drift + facet color wave + iridescent edge shift
 *   Treble = grid brightness + edge sparks + facet flicker + scan acceleration
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// ── Noise helpers ───────────────────────────────────────────────
// Uses common.glsl's integer-based hash21/noise2D for cross-backend
// consistency (sin()-based hashes produce different values on Vulkan
// vs OpenGL due to transcendental precision differences).

float fbm(in vec2 uv, int octaves, float rotAngle) {
    float value = 0.0;
    float amplitude = 0.5;
    float c = cos(rotAngle);
    float s = sin(rotAngle);
    mat2 rot = mat2(c, -s, s, c);
    for (int i = 0; i < octaves && i < 8; i++) {
        value += amplitude * noise2D(uv);
        uv = rot * uv * 2.0 + vec2(180.0);
        amplitude *= 0.55;
    }
    return value;
}

// ── CachyOS palette ─────────────────────────────────────────────

vec3 cachyPalette(float t, vec3 primary, vec3 secondary, vec3 accent) {
    t = fract(t);
    if (t < 0.33)      return mix(primary, secondary, t * 3.0);
    else if (t < 0.66) return mix(secondary, accent, (t - 0.33) * 3.0);
    else                return mix(accent, primary, (t - 0.66) * 3.0);
}

// ── SDF primitives ──────────────────────────────────────────────

float sdTriangle(vec2 p, vec2 a, vec2 b, vec2 c) {
    vec2 e0 = b - a, e1 = c - b, e2 = a - c;
    vec2 v0 = p - a, v1 = p - b, v2 = p - c;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d0 = vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x));
    vec2 d1 = vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x));
    vec2 d2 = vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x));
    vec2 d = min(min(d0, d1), d2);
    return -sqrt(d.x) * sign(d.y);
}

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Polygon SDF — correct for any simple polygon (convex or concave).
// Uses winding-number sign rule (Inigo Quilez).
float sdPolygon11(vec2 p, vec2 v[11]) {
    float d = dot(p - v[0], p - v[0]);
    float s = 1.0;
    for (int i = 0, j = 10; i < 11; j = i, i++) {
        vec2 e = v[j] - v[i];
        vec2 w = p - v[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= v[i].y, p.y < v[j].y, e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }
    return s * sqrt(d);
}


// ═══════════════════════════════════════════════════════════════
//  CACHYOS LOGO GEOMETRY — extracted from official SVG
//  All coordinates normalized to 0-1 (viewBox 17.921 x 17.921)
// ═══════════════════════════════════════════════════════════════

const vec2 V0  = vec2(0.2217, 0.1054);
const vec2 V1  = vec2(0.2415, 0.1074);
const vec2 V2  = vec2(0.7073, 0.1074);
const vec2 V3  = vec2(0.5894, 0.3112);
const vec2 V4  = vec2(0.3368, 0.3112);
const vec2 V5  = vec2(0.2316, 0.4932);
const vec2 V6  = vec2(0.0007, 0.4876);
const vec2 V7  = vec2(0.0553, 0.5814);
const vec2 V8  = vec2(0.3383, 0.6777);
const vec2 V9  = vec2(0.2316, 0.8866);
const vec2 V10 = vec2(0.7100, 0.8866);
const vec2 V11 = vec2(0.8309, 0.6777);

const vec2  CIRC_POS_0 = vec2(0.9202, 0.5939);
const vec2  CIRC_POS_1 = vec2(0.7527, 0.4088);
const vec2  CIRC_POS_2 = vec2(0.8358, 0.2000);
const float CIRC_RAD_0 = 0.0794;
const float CIRC_RAD_1 = 0.0603;
const float CIRC_RAD_2 = 0.0311;

const vec2 LOGO_CENTER = vec2(0.40, 0.50);

// ── Logo SDF evaluation ─────────────────────────────────────────

struct LogoHit {
    float dist;
    int   facetId;
    vec3  color;
    float edgeDist;
};

LogoHit evalLogo(vec2 p, vec2 displacement[12], vec3 palCyan, vec3 palTeal, vec3 palMint, vec3 palGlow) {
    LogoHit hit;
    hit.dist = 1e9;
    hit.facetId = -1;
    hit.color = vec3(0.0);
    hit.edgeDist = 1e9;

    vec2 v0  = V0  + displacement[0];
    vec2 v2  = V2  + displacement[1];
    vec2 v3  = V3  + displacement[2];
    vec2 v4  = V4  + displacement[3];
    vec2 v5  = V5  + displacement[4];
    vec2 v6  = V6  + displacement[5];
    vec2 v7  = V7  + displacement[6];
    vec2 v8  = V8  + displacement[7];
    vec2 v9  = V9  + displacement[8];
    vec2 v10 = V10 + displacement[9];
    vec2 v11 = V11 + displacement[10];

    float d;

    // ── Cyan body: full "C" silhouette as a proper polygon SDF ──
    // The old fan-decomposition from V0 created triangles that extended
    // beyond the concave polygon boundary, causing overlap artifacts
    // and color bleeding where teal should dominate.
    vec2 cBody[11] = vec2[11](v0, v2, v3, v4, v5, v8, v11, v10, v9, v7, v6);
    float cyanDist = sdPolygon11(p, cBody);

    // ── Teal facets (foreground layer) ──────────────────────────
    // Each adjacent pair shares exactly one diagonal so they tile
    // without overlap.  Previous code used two different diagonals
    // per quad, causing SDF overlap and facet-color bleeding.
    float tealDist = 1e9;
    int tealFacet = -1;

    d = sdTriangle(p, v0, v3, v2);                     // top-right  (quad v0-v2-v3-v4, diagonal v0-v3)
    if (d < tealDist) { tealDist = d; tealFacet = 0; }
    d = sdTriangle(p, v0, v4, v3);                     // top-left   (quad v0-v2-v3-v4, diagonal v0-v3)
    if (d < tealDist) { tealDist = d; tealFacet = 1; }
    d = sdTriangle(p, v4, v0, v5);                     // mid-right  (quad v0-v4-v5-v6, diagonal v0-v5)
    if (d < tealDist) { tealDist = d; tealFacet = 2; }
    d = sdTriangle(p, v0, v5, v6);                     // mid-left   (quad v0-v4-v5-v6, diagonal v0-v5)
    if (d < tealDist) { tealDist = d; tealFacet = 3; }
    d = sdTriangle(p, v5, v8, v9);                     // lower-right (quad v5-v8-v9-v7, diagonal v5-v9)
    if (d < tealDist) { tealDist = d; tealFacet = 4; }
    d = sdTriangle(p, v5, v9, v7);                     // lower-left  (quad v5-v8-v9-v7, diagonal v5-v9)
    if (d < tealDist) { tealDist = d; tealFacet = 5; }
    d = sdTriangle(p, v8, v9, v10);                    // bottom
    if (d < tealDist) { tealDist = d; tealFacet = 6; }

    // ── Painter's order: teal always on top of cyan ─────────────
    // The old approach used SDF depth comparison (min) which let the
    // larger cyan fan triangles "win" over smaller teal triangles
    // in overlapping regions. Now we explicitly enforce paint order.
    if (tealDist <= 0.0) {
        hit.dist = tealDist;
        hit.facetId = tealFacet;
        hit.color = palTeal;
    } else if (cyanDist <= 0.0) {
        hit.dist = cyanDist;
        hit.facetId = 7;
        hit.color = palCyan;
        if (sdTriangle(p, v8, v10, v11) < 0.0) hit.facetId = 8;
    } else {
        // Outside the logo — nearest distance for glow effects
        hit.dist = min(cyanDist, tealDist);
        if (tealDist < cyanDist) {
            hit.facetId = tealFacet;
            hit.color = palTeal;
        } else {
            hit.facetId = 7;
            hit.color = palCyan;
        }
    }

    // Circles
    vec2 cp0 = CIRC_POS_0 + displacement[11];
    d = length(p - cp0) - CIRC_RAD_0;
    if (d < hit.dist) { hit.dist = d; hit.facetId = 9; hit.color = palCyan; }
    vec2 cp1 = CIRC_POS_1 + displacement[11] * 0.7;
    d = length(p - cp1) - CIRC_RAD_1;
    if (d < hit.dist) { hit.dist = d; hit.facetId = 10; hit.color = palCyan; }
    vec2 cp2 = CIRC_POS_2 + displacement[11] * 0.4;
    d = length(p - cp2) - CIRC_RAD_2;
    if (d < hit.dist) { hit.dist = d; hit.facetId = 11; hit.color = palCyan; }

    // Shared edges (internal diagonals + polygon edges between facets)
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v0, v3));
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v3, v2));
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v4, v3));
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v0, v4));   // diagonal between Facet 1 & 2
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v0, v5));
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v4, v5));
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v6, v5));
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v5, v8));
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v5, v9));   // diagonal between Facet 4 & 5
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v5, v7));
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v8, v9));
    hit.edgeDist = min(hit.edgeDist, sdSegment(p, v8, v11));

    return hit;
}

// ── Per-instance logo evaluation (displacement + SDF) ────────────

LogoHit evalLogoInstance(vec2 logoUV, int idx, float time,
                         float bassEnv, float logoPulse,
                         vec3 palCyan, vec3 palTeal, vec3 palMint, vec3 palGlow) {
    vec2 displacement[12];
    for (int i = 0; i < 12; i++) {
        float phase = float(i) * 2.39996 + float(idx) * 1.618;
        vec2 wobble = vec2(
            sin(time * 0.7 + phase) * noise2D(vec2(float(i) * 7.3 + float(idx) * 100.0, time * 0.3)),
            cos(time * 0.9 + phase) * noise2D(vec2(time * 0.3, float(i) * 11.1 + float(idx) * 100.0))
        ) * 0.004;
        vec2 vertPos = (i < 11) ? vec2[11](V0,V2,V3,V4,V5,V6,V7,V8,V9,V10,V11)[i] : CIRC_POS_0;
        vec2 scatterDir = normalize(vertPos - LOGO_CENTER + 0.001);
        float scatter = bassEnv * logoPulse * 0.06;
        displacement[i] = wobble + scatterDir * scatter;
    }
    return evalLogo(logoUV, displacement, palCyan, palTeal, palMint, palGlow);
}

// ── Per-instance UV computation ──────────────────────────────────
// Returns logo-space UV; instScale written via out parameter.

vec2 computeInstanceUV(int idx, int totalCount, vec2 globalUV, float aspect, float time,
                       float logoScale, float bassEnv, float logoPulse,
                       float sizeMin, float sizeMax, out float instScale) {
    vec2 uv = globalUV;
    uv.x = (uv.x - 0.5) * aspect + 0.5;

    if (totalCount <= 1) {
        // Original single-logo behavior: gentle Lissajous near screen center
        vec2 drift = vec2(
            sin(time * 0.17) * 0.012 + sin(time * 0.31) * 0.006,
            cos(time * 0.23) * 0.010 + cos(time * 0.13) * 0.005
        );
        uv -= drift;
        float rotAng = sin(time * 0.15) * 0.05;
        vec2 lp = uv - vec2(0.5);
        uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
                   lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);
        float breathe = 1.0 + sin(time * 0.8) * 0.015;
        float springT = fract(time * 1.5);
        float spring = 1.0 + bassEnv * 0.1 * exp(-springT * 6.0) * cos(springT * 20.0);
        instScale = logoScale * breathe * spring;
        uv = (uv - 0.5) / instScale + LOGO_CENTER;
        return uv;
    }

    // Multi-instance: wide roaming Lissajous with unique frequencies
    float h1 = hash21(vec2(float(idx) * 7.31, 3.17));
    float h2 = hash21(vec2(float(idx) * 13.71, 7.23));
    float h3 = hash21(vec2(float(idx) * 5.13, 11.37));
    float h4 = hash21(vec2(float(idx) * 9.77, 17.53));

    float roam = 0.35;
    float f1 = 0.07 + float(idx) * 0.023;
    float f2 = 0.05 + float(idx) * 0.019;
    vec2 drift = vec2(
        sin(time * f1 + h1 * TAU) * roam + sin(time * f1 * 2.3 + h3 * TAU) * roam * 0.3,
        cos(time * f2 + h2 * TAU) * roam * 0.9 + cos(time * f2 * 1.7 + h4 * TAU) * roam * 0.25
    );
    uv -= drift;

    // Per-instance rotation oscillation
    float rotAng = sin(time * (0.1 + float(idx) * 0.027) + h4 * TAU) * 0.06;
    vec2 lp = uv - vec2(0.5);
    uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
               lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);

    // Per-instance scale from size range
    instScale = mix(sizeMin, sizeMax, h3) * logoScale;
    float breathe = 1.0 + sin(time * (0.6 + float(idx) * 0.13) + h1 * TAU) * 0.015;
    float springT = fract(time * 1.5 + h2);
    float spring = 1.0 + bassEnv * 0.1 * exp(-springT * 6.0) * cos(springT * 20.0);
    instScale *= breathe * spring;
    uv = (uv - 0.5) / instScale + LOGO_CENTER;
    return uv;
}

// ── Faceted diamond grid ────────────────────────────────────────

vec3 facetGrid(vec2 p, float scale) {
    p *= scale;
    vec2 d = vec2(p.x + p.y, p.x - p.y) * 0.7071;
    vec2 id = floor(d);
    vec2 f = fract(d) - 0.5;
    float edgeDist = 1.0 - 2.0 * max(abs(f.x), abs(f.y));
    float cellHash = hash21(id);
    vec2 vf = fract(d);
    float vertexDist = min(min(length(vf), length(vf - vec2(1.0, 0.0))),
                          min(length(vf - vec2(0.0, 1.0)), length(vf - vec2(1.0, 1.0))));
    return vec3(edgeDist, cellHash, vertexDist);
}


vec4 renderCachyZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params,
                     bool isHighlighted, float bass, float mids, float treble, float overall,
                     bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);

    float speed         = customParams[0].x >= 0.0 ? customParams[0].x : 0.12;
    float flowSpeed     = customParams[0].y >= 0.0 ? customParams[0].y : 0.25;
    float noiseScale    = customParams[0].z >= 0.0 ? customParams[0].z : 3.5;
    int octaves         = int(customParams[0].w >= 0.0 ? customParams[0].w : 6.0);

    float gridScale     = customParams[1].x >= 0.0 ? customParams[1].x : 4.0;
    float gridStrength  = customParams[1].y >= 0.0 ? customParams[1].y : 0.15;
    float brightness    = customParams[1].z >= 0.0 ? customParams[1].z : 0.6;
    float contrast      = customParams[1].w >= 0.0 ? customParams[1].w : 0.9;

    float fillOpacity       = customParams[2].x >= 0.0 ? customParams[2].x : 0.85;
    float borderGlow        = customParams[2].y >= 0.0 ? customParams[2].y : 0.35;
    float edgeFadeStart     = customParams[2].z >= 0.0 ? customParams[2].z : 30.0;
    float borderBrightness  = customParams[2].w >= 0.0 ? customParams[2].w : 1.4;

    float audioReact    = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;
    float particleStr   = customParams[3].y >= 0.0 ? customParams[3].y : 0.3;
    float innerGlowStr  = customParams[3].z >= 0.0 ? customParams[3].z : 0.3;
    float sparkleStr    = customParams[3].w >= 0.0 ? customParams[3].w : 2.0;

    float fbmRot        = customParams[4].w >= 0.0 ? customParams[4].w : 0.6;
    float flowDirection = customParams[5].x >= 0.0 ? customParams[5].x : 0.3;

    float logoScale     = customParams[5].y >= 0.0 ? customParams[5].y : 0.5;
    float logoIntensity = customParams[5].z >= 0.0 ? customParams[5].z : 0.6;
    float logoPulse     = customParams[5].w >= 0.0 ? customParams[5].w : 0.8;

    int   logoCount     = clamp(int(customParams[6].x >= 0.0 ? customParams[6].x : 4.0), 1, 8);
    float logoSizeMin   = customParams[6].y >= 0.0 ? customParams[6].y : 0.4;
    float logoSizeMax   = customParams[6].z >= 0.0 ? customParams[6].z : 1.0;

    float flowCenterX   = customParams[6].w >= -1.5 ? customParams[6].w : 0.4;
    float flowCenterY   = customParams[7].x >= -1.5 ? customParams[7].x : 0.5;

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;

    vec2 p = fragCoord - center;
    float d = sdRoundedBox(p, halfSize, borderRadius);
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    float time = iTime;

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, vec3(0.0, 0.8, 1.0));
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, vec3(0.0, 0.667, 0.533));
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, vec3(0.0, 1.0, 0.8));
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, vec3(0.13, 1.0, 0.71));

    float vitality = isHighlighted ? 1.0 : 0.3;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(time * 0.8 * PI)) * 0.5;

    float flowAngle = flowDirection * TAU;
    vec2 flowDir = vec2(cos(flowAngle), sin(flowAngle));

    // ── Audio envelopes ────────────────────────────────────────
    float bassEnv   = hasAudio ? smoothstep(0.02, 0.3, bass) * audioReact : 0.0;
    float midsEnv   = hasAudio ? smoothstep(0.02, 0.4, mids) * audioReact : 0.0;
    float trebleEnv = hasAudio ? smoothstep(0.05, 0.5, treble) * audioReact : 0.0;

    // ── Background UV (no audio warp — audio modulates parameters instead) ──
    vec2 centeredUV = (globalUV * 2.0 - 1.0) * noiseScale;
    centeredUV.x *= aspect;

    vec4 result = vec4(0.0);

    if (d < 0.0) {

        // ── Background: living domain-warped FBM ────────────────
        // Audio modulates the existing animation organically:
        //   bass  → flow accelerates, veins brighten
        //   mids  → palette drifts warmer/cooler
        //   treble → grid catches light
        float audioColorShift = midsEnv * 0.15;

        vec2 flowCenter = (vec2(flowCenterX, flowCenterY) * 2.0 - 1.0) * noiseScale;
        flowCenter.x *= aspect;
        vec2 toLogo = flowCenter - centeredUV;
        float pullStrength = 0.15 / (length(toLogo) + 0.1);
        vec2 flowUV = centeredUV + (flowDir * flowSpeed + normalize(toLogo + 0.001) * pullStrength) * time;

        float q = fbm(flowUV + time * speed, octaves, fbmRot);
        float r = fbm(flowUV + q * 1.5 + time * speed * 0.7, octaves, fbmRot);

        // Audio-reactive veins with domain warping
        vec2 veinUV = centeredUV * 1.3 + flowDir * time * flowSpeed * 0.5;

        // Domain warp: bass makes veins slither, mids add slower undulation
        float warpStr = 0.08 + bassEnv * 0.25 + midsEnv * 0.12;
        float wn1 = noise2D(veinUV * 1.8 + time * 0.6);
        float wn2 = noise2D(veinUV * 1.8 + time * 0.6 + vec2(97.0, 53.0));
        veinUV += (vec2(wn1, wn2) - 0.5) * warpStr;

        // Second warp layer driven by treble — fast jittery distortion
        float trebleWarp = trebleEnv * 0.1;
        if (trebleWarp > 0.005) {
            float tw1 = noise2D(veinUV * 4.0 + time * 3.0);
            float tw2 = noise2D(veinUV * 4.0 + time * 3.0 + vec2(41.0, 73.0));
            veinUV += (vec2(tw1, tw2) - 0.5) * trebleWarp;
        }

        float veinNoise = fbm(veinUV, max(octaves - 2, 3), fbmRot);
        float veinWidth = 0.03 + bassEnv * 0.02;
        float veins = smoothstep(veinWidth, 0.0, abs(veinNoise - 0.5)) * 0.35;

        // Vein brightness pulse along their length on bass hits
        float veinPulse = 1.0 + bassEnv * 0.4 * sin(veinNoise * 12.0 - time * 4.0);

        float colorT = r * contrast + audioColorShift;
        vec3 col = cachyPalette(colorT, palPrimary, palSecondary, palAccent);
        col *= brightness * 0.55;

        vec3 veinCol = cachyPalette(colorT + 0.2 + audioColorShift, palAccent, palGlow, palPrimary);
        col += veinCol * veins * veinPulse;

        vec3 grid = facetGrid(centeredUV + time * speed * 0.3, gridScale);
        float edgeLine = smoothstep(0.05, 0.0, grid.x * 0.5);
        float gridAudio = 1.0 + trebleEnv * 0.8;
        vec3 gridColor = cachyPalette(grid.y + colorT * 0.3, palPrimary, palSecondary, palAccent);
        col = mix(col, gridColor * 0.4 * gridAudio, edgeLine * gridStrength);

        // ── Multi-instance logo rendering ─────────────────────────
        for (int li = 0; li < logoCount && li < 8; li++) {
            float instScale;
            vec2 iLogoUV = computeInstanceUV(li, logoCount, globalUV, aspect, time,
                                              logoScale, bassEnv, logoPulse,
                                              logoSizeMin, logoSizeMax, instScale);

            // Bounding check: skip if fragment is far from this logo
            if (iLogoUV.x < -0.2 || iLogoUV.x > 1.15 ||
                iLogoUV.y < -0.1 || iLogoUV.y > 1.1) continue;

            // Depth-based intensity: smaller logos are dimmer (parallax depth cue)
            float maxScale = logoSizeMax * logoScale;
            float depthFactor = clamp(instScale / max(maxScale, 0.01), 0.0, 1.0);
            float instIntensity = logoIntensity * (0.3 + 0.7 * depthFactor);

            LogoHit iLogo = evalLogoInstance(iLogoUV, li, time, bassEnv, logoPulse,
                                             palPrimary, palSecondary, palAccent, palGlow);

            if (iLogo.dist > 0.08) continue;

            // ── Light casting (neon sign in fog) ──────────────────
            float lightCast = exp(-max(iLogo.dist, 0.0) * 15.0) * 0.25;
            vec3 logoLight = cachyPalette(time * 0.08 + iLogoUV.y + float(li) * 0.3,
                                           palGlow, palPrimary, palAccent);
            col += logoLight * lightCast * instIntensity * (1.0 + bassEnv * 0.2) * depthFactor;

            // ── Per-instance bass shockwave ring ──────────────────
            float iShockPhase = fract(time * 0.7 + float(li) * 0.137);
            float iShockStr = bassEnv * (1.0 - iShockPhase) * logoPulse;
            if (iShockStr > 0.01) {
                float iLogoDist = length(iLogoUV - LOGO_CENTER);
                float iShockRadius = iShockPhase * 0.5;
                float shockDist = abs(iLogoDist - iShockRadius);
                float shockMask = smoothstep(0.06, 0.0, shockDist) * iShockStr;
                vec3 shockCol = cachyPalette(iShockRadius * 3.0 + time * 0.2 + float(li),
                                              palGlow, palPrimary, palAccent);
                col += shockCol * shockMask * 0.15 * depthFactor;
            }

            // ── Logo fill ─────────────────────────────────────────
            if (iLogo.dist < 0.03) {
                if (iLogo.dist < 0.0) {
                    float facetSeed = float(iLogo.facetId) * 3.7 + float(li) * 17.0;
                    float facetPhase = float(iLogo.facetId) * 2.39996 + float(li) * 1.618;

                    float warpIntensity = 1.5 + midsEnv * 2.5;
                    vec2 energyUV = centeredUV * 1.5 + flowDir * time * flowSpeed * 1.5 + vec2(facetSeed);
                    float eq = fbm(energyUV + time * speed * 1.2, max(octaves - 1, 3), fbmRot);
                    float er = fbm(energyUV + eq * warpIntensity + time * speed, max(octaves - 1, 3), fbmRot);
                    float energyT = er * contrast * 1.3;

                    float colorRate = 0.08 + float(iLogo.facetId) * 0.013 + float(li) * 0.007;
                    float facetColorT = energyT + time * colorRate + facetPhase * 0.3;

                    float spatialT = dot(iLogoUV, vec2(0.7, 0.3));
                    float colorWave = sin(spatialT * 8.0 - time * 2.0) * midsEnv * 0.5;
                    facetColorT += colorWave;

                    vec3 facetCol = cachyPalette(facetColorT, palPrimary, palAccent, palGlow) * brightness * 1.8;

                    float wavePos = fract(time * 0.25 + float(li) * 0.11);
                    float facetT = float(iLogo.facetId) / 12.0;
                    float topoWave = exp(-8.0 * pow(fract(facetT - wavePos), 2.0));
                    facetCol *= 1.0 + topoWave * 0.4;

                    facetCol *= 1.0 + bassEnv * logoPulse * 0.3;

                    int flickerFacet = int(mod(floor(time * 12.0 + float(li) * 3.7), 12.0));
                    if (iLogo.facetId == flickerFacet && trebleEnv > 0.1) {
                        facetCol = mix(facetCol, vec3(1.0) * brightness * 2.5, trebleEnv * 0.5);
                    }

                    if (iLogo.facetId >= 9) {
                        float gradT = (iLogoUV.y - 0.3) * 2.0;
                        facetCol = mix(facetCol, palGlow * brightness * 2.0, clamp(gradT, 0.0, 0.5));
                        float circPulse = sin(time * (2.0 + float(iLogo.facetId - 9) * 0.7) + facetPhase);
                        facetCol *= 0.9 + 0.15 * circPulse;
                    }

                    float fresnelLike = smoothstep(-0.015, -0.001, iLogo.dist);
                    facetCol += palGlow * fresnelLike * 0.25 * (1.0 + midsEnv * 0.4);

                    float aa = smoothstep(0.0, -0.003, iLogo.dist);
                    col = mix(col, facetCol * instIntensity, aa);
                }

                // ── Multi-layer glow ──────────────────────────────
                float glow1 = exp(-max(iLogo.dist, 0.0) * 80.0) * 0.5;
                float glow2 = exp(-max(iLogo.dist, 0.0) * 25.0) * 0.25;
                float glow3 = exp(-max(iLogo.dist, 0.0) * 8.0) * 0.12;
                vec3 edgeCol = cachyPalette(time * 0.12 + iLogoUV.y + float(li) * 0.2,
                                             palGlow, palPrimary, palAccent);
                float flare = 1.0 + bassEnv * 0.6;
                col += edgeCol * glow1 * flare * particleStr * 2.0 * depthFactor;
                col += palPrimary * glow2 * flare * 0.5 * depthFactor;
                col += palAccent * glow3 * 0.4 * depthFactor;
            }

            // ── Energy veins along facet seams ────────────────────
            if (iLogo.dist < 0.0 && iLogo.edgeDist < 0.015) {
                float crease = smoothstep(0.012, 0.001, iLogo.edgeDist);
                float edgeFlow = noise2D(iLogoUV * 30.0 + flowDir * time * 2.0 + float(li) * 50.0);
                float flowPulse = smoothstep(0.3, 0.7, edgeFlow);
                vec3 creaseCol = mix(palGlow, vec3(1.0), 0.3);
                col += creaseCol * crease * (0.25 + flowPulse * 0.3) * depthFactor;

                if (trebleEnv > 0.01) {
                    float sparkN = noise2D(iLogoUV * 50.0 + time * 8.0 + float(li) * 77.0);
                    float spark = step(0.92, sparkN) * trebleEnv * sparkleStr;
                    col += vec3(1.0) * crease * spark * depthFactor;
                }
            }

            // ── Treble edge discharge ─────────────────────────────
            if (trebleEnv > 0.01 && iLogo.dist > -0.005 && iLogo.dist < 0.02) {
                float sparkN = noise2D(iLogoUV * 35.0 + time * 7.0 + float(li) * 33.0);
                sparkN = smoothstep(0.5, 0.95, sparkN);
                float edgeMask = smoothstep(0.02, 0.0, abs(iLogo.dist));
                col += mix(palGlow, vec3(1.0), 0.5) * sparkN * edgeMask * trebleEnv * sparkleStr * depthFactor;
            }

            // ── Scanning beam ─────────────────────────────────────
            {
                float scanSpeed = 0.3 + trebleEnv * 1.5;
                float scanPos = fract(time * scanSpeed * 0.12 + float(li) * 0.23);
                float beamDist = abs(iLogoUV.y - scanPos);
                float beam = smoothstep(0.025, 0.0, beamDist);
                float beamWide = smoothstep(0.07, 0.0, beamDist) * 0.2;
                float beamMask = smoothstep(0.02, -0.003, iLogo.dist);
                vec3 beamCol = mix(palPrimary, vec3(1.0), 0.35);
                col += beamCol * (beam + beamWide) * beamMask * instIntensity * 0.6 * depthFactor;
                col += palPrimary * beam * beamMask * 0.1 * smoothstep(0.3, 0.0, abs(iLogoUV.x - 0.5)) * depthFactor;
            }
        } // end logo instance loop

        // ── Ambient particles ─────────────────────────────────────
        {
            float particleLayer = 0.0;
            for (int pi = 0; pi < 3; pi++) {
                float pScale = 10.0 + float(pi) * 5.0;
                float pSpeed = 0.25 + float(pi) * 0.12;
                vec2 pUV = centeredUV * pScale + flowDir * time * pSpeed;
                vec2 pCell = floor(pUV);
                vec2 pFract = fract(pUV);
                vec2 pOffset = hash22(pCell) * 0.6 + 0.2;
                float pDist = length(pFract - pOffset);
                float pRadius = 0.06 - float(pi) * 0.012;
                float particle = smoothstep(pRadius, pRadius * 0.2, pDist);
                float twinkle = 0.5 + 0.5 * sin(hash21(pCell) * TAU + time * 3.5);
                particleLayer += particle * twinkle;
            }
            col += mix(palAccent, palGlow, 0.6) * particleLayer * particleStr * 0.5;
        }

        // ── Vitality ───────────────────────────────────────────
        if (isHighlighted) {
            col *= 1.1;
        } else {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.25);
            col *= 0.7 + idlePulse * 0.08;
        }

        // ── Inner edge glow (iridescent) ────────────────────────
        float innerDist = -d;
        float depthDarken = smoothstep(0.0, edgeFadeStart, innerDist);
        col *= mix(0.6, 1.0, 1.0 - depthDarken * 0.35);

        float innerGlow = exp(-innerDist / 12.0);
        float edgeAngle = atan(p.y, p.x);
        float iriT = edgeAngle / TAU + time * 0.05 + midsEnv * 0.2;
        vec3 iriCol = cachyPalette(iriT, palPrimary, palSecondary, palAccent);
        col += iriCol * innerGlow * innerGlowStr;

        col = mix(col, fillColor.rgb * luminance(col), 0.15);

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // ── Border ───────────────────────────────────────────────
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float angle = atan(p.y, p.x) * 2.0;
        float borderFlow = fbm(vec2(sin(angle), cos(angle)) * 2.0 + time * 0.4, 3, 0.5);
        vec3 borderCol = cachyPalette(borderFlow * contrast + midsEnv * 0.2,
                                       palPrimary, palSecondary, palAccent);
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

    // ── Outer glow ───────────────────────────────────────────
    float bassGlowPush = hasAudio ? bassEnv * 2.0 : idlePulse * 5.0;
    float glowRadius = mix(10.0, 18.0, vitality) + bassGlowPush;
    if (d > 0.0 && d < glowRadius && borderGlow > 0.01) {
        float glow = expGlow(d, 7.0, borderGlow);
        float angle = atan(p.y, p.x);
        float glowT = angularNoise(angle, 1.5, time * 0.08) + midsEnv * 0.15;
        vec3 glowCol = cachyPalette(glowT, palPrimary, palSecondary, palAccent);
        glowCol *= mix(0.3, 1.0, vitality);
        result.rgb += glowCol * glow * 0.5;
        result.a = max(result.a, glow * 0.4);
    }

    return result;
}

// ─── Custom Label Composite ───────────────────────────────────────

vec4 compositeCachyLabels(vec4 color, vec2 fragCoord,
                          float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, vec3(0.0, 0.8, 1.0));
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, vec3(0.0, 0.667, 0.533));
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, vec3(0.0, 1.0, 0.8));
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, vec3(0.13, 1.0, 0.71));

    float labelGlowSpread = customParams[4].x >= 0.0 ? customParams[4].x : 3.0;
    float labelBrightness = customParams[4].y >= 0.0 ? customParams[4].y : 2.5;
    float labelAudioReact = customParams[4].z >= 0.0 ? customParams[4].z : 1.0;
    float labelChroma     = customParams[7].z >= 0.0 ? customParams[7].z : 0.5;

    float bassR   = hasAudio ? bass * labelAudioReact   : 0.0;
    float midsR   = hasAudio ? mids * labelAudioReact   : 0.0;
    float trebleR = hasAudio ? treble * labelAudioReact : 0.0;

    // ── Expanded Gaussian halo (wide + visible at any resolution) ──
    // Sample in a wide 5×5 grid with large spread for a clearly visible glow.
    float haloSmooth = 0.0;
    float haloWide = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float r2 = float(dx * dx + dy * dy);
            vec2 off = vec2(float(dx), float(dy)) * px * labelGlowSpread;
            float s = texture(uZoneLabels, uv + off).a;
            haloSmooth += s * exp(-r2 * 0.4);   // tight core
            haloWide   += s * exp(-r2 * 0.15);   // wide outer
        }
    }
    haloSmooth /= 8.0;
    haloWide   /= 12.0;

    // Second wider pass for the large atmospheric glow
    float haloAtmo = 0.0;
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            float r2 = float(dx * dx + dy * dy);
            if (r2 > 12.0) continue;  // skip corners
            vec2 off = vec2(float(dx), float(dy)) * px * labelGlowSpread * 1.8;
            haloAtmo += texture(uZoneLabels, uv + off).a * exp(-r2 * 0.12);
        }
    }
    haloAtmo /= 18.0;

    float haloAngle = atan(uv.y - 0.5, uv.x - 0.5);

    if (haloAtmo > 0.001) {
        float outerMask = haloAtmo * (1.0 - labels.a);
        float innerMask = haloSmooth * (1.0 - labels.a);
        float midMask   = haloWide * (1.0 - labels.a);

        // Chromatic split — R and B offset outward from text center
        float chromaOff = (3.0 + bassR * 4.0) * px.x * labelGlowSpread * labelChroma;
        vec2 chromaDir = vec2(cos(haloAngle), sin(haloAngle));
        float chromR = texture(uZoneLabels, uv + chromaDir * chromaOff).a * (1.0 - labels.a);
        float chromB = texture(uZoneLabels, uv - chromaDir * chromaOff).a * (1.0 - labels.a);

        // Angular color sweep
        float sweep = fract(haloAngle / TAU + iTime * 0.12 + midsR * 0.4);
        vec3 haloCol = cachyPalette(sweep, palPrimary, palSecondary, palAccent);

        // Inner ring: bright, tight, chromatic
        vec3 innerCol = vec3(chromR * 1.5, innerMask, chromB * 1.5) * labelChroma
                      + haloCol * (1.0 - labelChroma * 0.5);
        color.rgb += innerCol * innerMask * (2.5 + bassR * 1.5);

        // Mid ring: colored glow
        color.rgb += haloCol * midMask * (1.5 + bassR * 0.8);

        // Outer atmospheric: soft wide glow
        vec3 outerCol = mix(haloCol, palGlow, 0.4 + midsR * 0.3);
        color.rgb += outerCol * outerMask * (0.8 + bassR * 0.5);

        // 12-fold angular rays — always visible, treble makes them sharper
        float rayAngle = mod(haloAngle, TAU / 12.0) - TAU / 24.0;
        float ray = exp(-abs(rayAngle) * (30.0 + trebleR * 40.0));
        float rayPulse = 0.6 + 0.4 * sin(iTime * 3.0);
        color.rgb += palGlow * 3.0 * ray * midMask * rayPulse;

        // Treble sparks
        if (trebleR > 0.04) {
            float sparkN = noise2D(uv * 50.0 + iTime * 5.0);
            float spark = smoothstep(0.55, 0.85, sparkN) * trebleR * 4.0;
            color.rgb += vec3(1.0, 0.95, 0.85) * innerMask * spark;
        }

        color.a = max(color.a, midMask * 0.8);
    }

    // ── Text fill: digital shatter pattern ─────────────────────
    // Noise-based angular regions create hard color boundaries inside
    // text strokes — a glitchy, digital aesthetic matching CachyOS.
    if (labels.a > 0.01) {
        // Noise at pixel scale — creates irregular angular regions inside text.
        // Each region gets a distinct color from the palette via threshold.
        vec2 nCoord = fragCoord * 0.08 + vec2(iTime * 0.6, iTime * 0.35);
        float n1 = noise2D(nCoord);
        float n2 = noise2D(nCoord * 1.7 + 50.0);

        // Quantize noise into 4 distinct bands — hard color boundaries
        float band = floor(n1 * 4.0) / 3.0;  // 0.0, 0.33, 0.67, 1.0
        vec3 bandCol = cachyPalette(band + iTime * 0.06, palPrimary, palSecondary, palAccent);

        // Second noise layer for brightness variation
        float bright = 0.6 + n2 * 0.8;  // range 0.6–1.4

        // Diagonal scan line — a bright stripe sweeping across the text
        float scanSpeed = 1.5 + bassR * 2.0;
        float scan = fract(fragCoord.x * 0.003 + fragCoord.y * 0.002 - iTime * scanSpeed * 0.04);
        float scanLine = smoothstep(0.0, 0.04, scan) * smoothstep(0.12, 0.04, scan);

        // Edge detection — bright rim at stroke boundaries
        float aL = texture(uZoneLabels, uv + vec2(-px.x, 0.0)).a;
        float aR = texture(uZoneLabels, uv + vec2( px.x, 0.0)).a;
        float aU = texture(uZoneLabels, uv + vec2(0.0, -px.y)).a;
        float aD = texture(uZoneLabels, uv + vec2(0.0,  px.y)).a;
        float edgeStrength = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);

        // Chromatic aberration — rotates over time
        float caOff = labelChroma * 2.0 * px.x * iResolution.x * 0.004;
        float caAngle = iTime * 0.8;
        vec2 caDir = vec2(cos(caAngle), sin(caAngle));
        float rCh = texture(uZoneLabels, uv + caDir * caOff).a;
        float bCh = texture(uZoneLabels, uv - caDir * caOff).a;
        vec3 chromaText = vec3(rCh, labels.a, bCh);

        // Build text color: banded noise body + scan highlight + edge rim
        vec3 textCol = bandCol * bright;
        textCol += palGlow * 2.5 * scanLine;                     // bright scan stripe
        textCol += mix(palGlow, vec3(1.0), 0.5) * edgeStrength;  // bright stroke edges
        textCol *= mix(vec3(1.0), chromaText + 0.4, labelChroma * 0.5);  // chroma tint

        // Bass breathing + per-band flash
        textCol *= (1.0 + bassR * 0.5);
        float flashBand = floor(n1 * 4.0);
        float flashPhase = hash21(vec2(flashBand, floor(iTime * 3.0)));
        if (flashPhase > 0.65 && bassR > 0.1) {
            textCol += palGlow * bassR * 1.5;
        }

        // Apply brightness
        textCol *= labelBrightness;

        // Gentle tonemap (openSUSE-style — preserves contrast)
        textCol = textCol / (0.6 + textCol);

        // Saturation boost
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
    float overall = getOverallSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderCachyZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (customParams[7].y > 0.5)
        color = compositeCachyLabels(color, fragCoord, bass, mids, treble, hasAudio);
    fragColor = clampFragColor(color);
}
