// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FEDORA DRIFT - Fragment Shader (Frosted Glass Infinity-F — Multi-Instance)
//
// Fedora "f" infinity symbol rendered as frosted glass volumetric fill
// against an icy data-stream sky with network node particles.
//
// Effects: data streams, frosted glass logo, network nodes,
//          dot matrix grid, orbital flow lines, pulse ring borders

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// ── Frost palette constants ─────────────────────────────────────
const vec3 FROST_BLUE   = vec3(0.318, 0.635, 0.855);   // #51A2DA
const vec3 FROST_DEEP   = vec3(0.161, 0.255, 0.447);   // #294172
const vec3 FROST_ICE    = vec3(0.475, 0.745, 0.910);   // #79BEE8
const vec3 FROST_SILVER = vec3(0.831, 0.937, 1.000);   // #D4EFFF


// ── Noise helpers ───────────────────────────────────────────────

float rand2D(in vec2 p) {
    return fract(sin(dot(p, vec2(15.285, 97.258))) * 47582.122);
}

vec2 quintic(vec2 f) {
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

float noise(in vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = rand2D(i);
    float b = rand2D(i + vec2(1.0, 0.0));
    float c = rand2D(i + vec2(0.0, 1.0));
    float d = rand2D(i + vec2(1.0, 1.0));
    vec2 u = quintic(f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(in vec2 uv, int octaves, float rotAngle) {
    float value = 0.0;
    float amplitude = 0.5;
    float c = cos(rotAngle), s = sin(rotAngle);
    mat2 rot = mat2(c, -s, s, c);
    for (int i = 0; i < octaves && i < 8; i++) {
        value += amplitude * noise(uv);
        uv = rot * uv * 2.0 + vec2(180.0);
        amplitude *= 0.55;
    }
    return value;
}


// ── Frost palette function ──────────────────────────────────────

vec3 frostPalette(float t, vec3 primary, vec3 secondary, vec3 accent) {
    t = fract(t);
    if (t < 0.33)      return mix(primary, secondary, t * 3.0);
    else if (t < 0.66) return mix(secondary, accent, (t - 0.33) * 3.0);
    else               return mix(accent, primary, (t - 0.66) * 3.0);
}


// ── SDF primitives ──────────────────────────────────────────────

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

float neonFlicker(float time, float seed, float trebleEnv) {
    float base = 0.92 + 0.08 * sin(time * 60.0 + seed * 100.0);
    float buzz = step(0.97, noise(vec2(time * 30.0, seed * 7.0))) * 0.4;
    float trebleBuzz = trebleEnv * step(0.9, noise(vec2(time * 50.0, seed * 13.0))) * 0.5;
    return clamp(base - buzz - trebleBuzz, 0.4, 1.0);
}


// ═══════════════════════════════════════════════════════════════
//  FEDORA "f" INFINITY LOGO — SDF GEOMETRY (exact SVG polygon)
// ═══════════════════════════════════════════════════════════════
//
//  Polygon vertices extracted directly from Fedora_icon_(2021).svg
//  white path, converted to shader-local coordinates.
//  Signed distance is negative inside, positive outside.

const vec2 LOGO_CENTER = vec2(0.50, 0.50);

// Kept for anti-aliasing and glow references elsewhere
const float TUBE_HW = 0.0425;

// Approximate center-of-mass of the logo (for flow-line orbits)
const vec2 LOGO_MID = vec2(-0.080, 0.087);

const int POLY_N = 120;
const vec2 POLY[120] = vec2[120](
    vec2(  0.145963, -0.289421),
    vec2(  0.185114, -0.274638),
    vec2(  0.219242, -0.251354),
    vec2(  0.246236, -0.220677),
    vec2(  0.263981, -0.183716),
    vec2(  0.270366, -0.141579),
    vec2(  0.270363, -0.137460),
    vec2(  0.270328, -0.133309),
    vec2(  0.270220, -0.129098),
    vec2(  0.269997, -0.124794),
    vec2(  0.269618, -0.120369),
    vec2(  0.269043, -0.115791),
    vec2(  0.266304, -0.104607),
    vec2(  0.260787, -0.094910),
    vec2(  0.252981, -0.087077),
    vec2(  0.243378, -0.081488),
    vec2(  0.232468, -0.078522),
    vec2(  0.220741, -0.078558),
    vec2(  0.209470, -0.081736),
    vec2(  0.199843, -0.087572),
    vec2(  0.192219, -0.095569),
    vec2(  0.186959, -0.105229),
    vec2(  0.184424, -0.116057),
    vec2(  0.184975, -0.127553),
    vec2(  0.185157, -0.129018),
    vec2(  0.185299, -0.130778),
    vec2(  0.185404, -0.132873),
    vec2(  0.185475, -0.135346),
    vec2(  0.185515, -0.138237),
    vec2(  0.185528, -0.141587),
    vec2(  0.182236, -0.164220),
    vec2(  0.173179, -0.182024),
    vec2(  0.159587, -0.195317),
    vec2(  0.142689, -0.204418),
    vec2(  0.123714, -0.209646),
    vec2(  0.103891, -0.211317),
    vec2(  0.084533, -0.208723),
    vec2(  0.066710, -0.201465),
    vec2(  0.051305, -0.190334),
    vec2(  0.039201, -0.176118),
    vec2(  0.031281, -0.159606),
    vec2(  0.028427, -0.141587),
    vec2(  0.028665, -0.120211),
    vec2(  0.028732, -0.098874),
    vec2(  0.028684, -0.077558),
    vec2(  0.028580, -0.056248),
    vec2(  0.028475, -0.034926),
    vec2(  0.028427, -0.013577),
    vec2(  0.102047, -0.014114),
    vec2(  0.126045, -0.008319),
    vec2(  0.140535,  0.007059),
    vec2(  0.145482,  0.027262),
    vec2(  0.140850,  0.047528),
    vec2(  0.126604,  0.063099),
    vec2(  0.102709,  0.069215),
    vec2(  0.028443,  0.069755),
    vec2(  0.028400,  0.083879),
    vec2(  0.028427,  0.093010),
    vec2(  0.028489,  0.098934),
    vec2(  0.028552,  0.103434),
    vec2(  0.028579,  0.108293),
    vec2(  0.028537,  0.115296),
    vec2(  0.028577,  0.118563),
    vec2(  0.028651,  0.127395),
    vec2(  0.028688,  0.140339),
    vec2(  0.028619,  0.155942),
    vec2(  0.028373,  0.172751),
    vec2(  0.027879,  0.189313),
    vec2(  0.017656,  0.234354),
    vec2( -0.003091,  0.274700),
    vec2( -0.032803,  0.308792),
    vec2( -0.069922,  0.335072),
    vec2( -0.112891,  0.351982),
    vec2( -0.160152,  0.357965),
    vec2( -0.210144,  0.351340),
    vec2( -0.255227,  0.332652),
    vec2( -0.293535,  0.303687),
    vec2( -0.323206,  0.266229),
    vec2( -0.342375,  0.222064),
    vec2( -0.349179,  0.172976),
    vec2( -0.341060,  0.122723),
    vec2( -0.320924,  0.077892),
    vec2( -0.290438,  0.040179),
    vec2( -0.251267,  0.011278),
    vec2( -0.205075, -0.007116),
    vec2( -0.153528, -0.013309),
    vec2( -0.093658, -0.013752),
    vec2( -0.093658,  0.069429),
    vec2( -0.153843,  0.069969),
    vec2( -0.182438,  0.073888),
    vec2( -0.208486,  0.083655),
    vec2( -0.230843,  0.098857),
    vec2( -0.248364,  0.119079),
    vec2( -0.259905,  0.143906),
    vec2( -0.264322,  0.172925),
    vec2( -0.260611,  0.200051),
    vec2( -0.250131,  0.224363),
    vec2( -0.233862,  0.244918),
    vec2( -0.212784,  0.260770),
    vec2( -0.187877,  0.270975),
    vec2( -0.160121,  0.274586),
    vec2( -0.132425,  0.271260),
    vec2( -0.107609,  0.261684),
    vec2( -0.086633,  0.246467),
    vec2( -0.070460,  0.226213),
    vec2( -0.060051,  0.201530),
    vec2( -0.056368,  0.173023),
    vec2( -0.056461, -0.142019),
    vec2( -0.056428, -0.144856),
    vec2( -0.056361, -0.147578),
    vec2( -0.056252, -0.150266),
    vec2( -0.056092, -0.153005),
    vec2( -0.055871, -0.155877),
    vec2( -0.055582, -0.158966),
    vec2( -0.045910, -0.195779),
    vec2( -0.027573, -0.228414),
    vec2( -0.001994, -0.255750),
    vec2(  0.029404, -0.276665),
    vec2(  0.065196, -0.290037),
    vec2(  0.103958, -0.294743)
);


// ── Signed distance to polygon (IQ winding-number method) ────
// Returns negative inside, positive outside.
float sdPolygon(vec2 p) {
    // AABB early-out: skip 120-edge loop for pixels clearly outside the polygon.
    // The polygon vertices span x ∈ [-0.350, 0.271], y ∈ [-0.295, 0.358].
    // If the point is far enough outside this box, return exact distance to AABB
    // (always ≤ true polygon distance, so a valid lower bound).
    vec2 dLo = vec2(-0.350, -0.295) - p;   // positive when p is left/below
    vec2 dHi = p - vec2(0.271, 0.358);     // positive when p is right/above
    vec2 outside = max(max(dLo, dHi), vec2(0.0));
    float boxDist2 = dot(outside, outside);
    if (boxDist2 > 0.0625) {               // > 0.25² — well outside polygon
        return sqrt(boxDist2);
    }

    float d = dot(p - POLY[0], p - POLY[0]);
    float s = 1.0;
    for (int i = 0, j = POLY_N - 1; i < POLY_N; j = i, i++) {
        vec2 e = POLY[j] - POLY[i];
        vec2 w = p - POLY[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 cond = bvec3(p.y >= POLY[i].y,
                            p.y <  POLY[j].y,
                            e.x * w.y > e.y * w.x);
        if (all(cond) || all(not(cond))) s *= -1.0;
    }
    return s * sqrt(d);
}


float fedoraSDF(vec2 p, float breathe) {
    return sdPolygon(p / breathe) * breathe;
}


// ═══════════════════════════════════════════════════════════════
//  EFFECT 1: DATA STREAM LINES (background)
// ═══════════════════════════════════════════════════════════════

vec3 dataStreams(vec2 uv, float time, float aspect, float bassEnv, float midsEnv, float trebleEnv,
                 vec3 palPrimary, vec3 palAccent, vec3 palGlow) {
    vec3 col = vec3(0.0);
    for (int i = 0; i < 6; i++) {
        float fi = float(i);
        float yBase = 0.15 + fi * 0.13;
        float freq = 1.2 + fi * 0.4;
        float speed = 0.3 + fi * 0.08;
        float phase = fi * 2.1;
        float path = yBase + sin(uv.x * freq * aspect + time * speed + phase) * 0.06
                   + noise(vec2(uv.x * 2.0 + time * 0.2, fi * 7.0)) * 0.03;
        float width = 0.008 + bassEnv * 0.004;
        float dist = abs(uv.y - path);
        float core = smoothstep(width, width * 0.1, dist);
        float glow = smoothstep(width * 6.0, 0.0, dist) * 0.15;

        // Bright pulse traveling along each stream
        float pulse = smoothstep(0.4, 0.5, sin(uv.x * 12.0 * aspect - time * (4.0 + trebleEnv * 2.0) + fi * 3.0));
        core *= 0.6 + pulse * 0.4;

        vec3 streamCol = mix(palPrimary, palAccent, fi / 6.0);
        streamCol = mix(streamCol, palGlow, midsEnv * 0.2);
        col += streamCol * (core + glow);
    }
    return col;
}


// ═══════════════════════════════════════════════════════════════
//  EFFECT 3: NETWORK NODE PARTICLES
// ═══════════════════════════════════════════════════════════════

vec3 networkNodes(vec2 uv, float time, float bassEnv, float midsEnv, float trebleEnv,
                  float particleStr, vec3 palPrimary, vec3 palAccent) {
    vec3 col = vec3(0.0);
    float gridSize = 6.0;

    // Current cell
    vec2 p = uv * gridSize;
    vec2 id = floor(p);
    vec2 f = fract(p);

    // Check 3x3 neighborhood for nodes and connections
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 nid = id + vec2(float(dx), float(dy));
            float h = hash21(nid + 0.5);
            // Only ~40% of cells have nodes
            if (h < 0.4) continue;

            // Node position with omnidirectional drift
            vec2 nodeOff = hash22(nid * 1.3 + 7.0) * 0.6 + 0.2;
            float driftX = sin(time * 0.3 + h * TAU) * 0.12;
            float driftY = cos(time * 0.25 + h * 5.0) * 0.12;
            vec2 nodePos = nid + nodeOff + vec2(driftX, driftY);
            vec2 diff = p - nodePos;
            float dist = length(diff);

            // Node dot
            float dotSize = 0.08 + bassEnv * 0.04;
            float dot = smoothstep(dotSize, dotSize * 0.15, dist);
            float dotGlow = exp(-dist * 4.0) * 0.08;

            // Treble flash on random nodes
            float flash = trebleEnv * 0.6 * step(0.75, hash21(nid + floor(time * 3.0)));
            dot *= 0.7 + flash;

            vec3 nodeCol = mix(palPrimary, palAccent, h);
            col += nodeCol * (dot + dotGlow) * particleStr;

            // Connections to neighbors — skip if node is far from this pixel
            // (connections span at most ~1.4 units; lineWidth ≈ 0.03)
            if (dist > 1.8) continue;

            for (int cy = -1; cy <= 1; cy++) {
                for (int cx = -1; cx <= 1; cx++) {
                    if (cx == 0 && cy == 0) continue;
                    vec2 nid2 = nid + vec2(float(cx), float(cy));
                    float h2 = hash21(nid2 + 0.5);
                    if (h2 < 0.4) continue;

                    // Connection appears/disappears periodically
                    float connPhase = sin(time * 0.5 + hash21(nid + nid2) * TAU);
                    if (connPhase < 0.2) continue;

                    vec2 nodeOff2 = hash22(nid2 * 1.3 + 7.0) * 0.6 + 0.2;
                    float driftX2 = sin(time * 0.3 + h2 * TAU) * 0.12;
                    float driftY2 = cos(time * 0.25 + h2 * 5.0) * 0.12;
                    vec2 nodePos2 = nid2 + nodeOff2 + vec2(driftX2, driftY2);

                    // Distance from point to line segment
                    float lineDist = sdSegment(p, nodePos, nodePos2);
                    float lineWidth = 0.03 + midsEnv * 0.015;
                    float line = smoothstep(lineWidth, lineWidth * 0.2, lineDist);
                    float connFade = (connPhase - 0.2) * 1.25; // fade in/out
                    col += palAccent * 0.15 * line * connFade * particleStr;
                }
            }
        }
    }

    return col;
}


// ═══════════════════════════════════════════════════════════════
//  EFFECT 4: DOT MATRIX GRID (background pattern)
// ═══════════════════════════════════════════════════════════════

vec3 dotMatrix(vec2 uv, float scale, float time, float bassEnv, float midsEnv, float trebleEnv,
               vec3 palPrimary, vec3 palAccent) {
    vec2 p = uv * scale;
    vec2 id = floor(p);
    vec2 f = fract(p) - 0.5;

    float dist = length(f);
    float radius = 0.08 + bassEnv * 0.03;
    float dot = smoothstep(radius, radius * 0.3, dist);

    // Diagonal brightness wave — mids modulate amplitude
    float wave = 0.4 + (0.3 + midsEnv * 0.2) * sin(id.x * 0.4 + id.y * 0.3 + time * 0.6);
    // Treble makes random dots flash
    wave += trebleEnv * 0.3 * step(0.7, hash21(id + floor(time * 4.0)));

    float glow = exp(-dist * 8.0) * 0.06;

    vec3 dotCol = mix(palPrimary, palAccent, wave);
    return dotCol * (dot * wave + glow);
}


// ═══════════════════════════════════════════════════════════════
//  EFFECT 5: FLOW LINES (orbital streamlines around logo)
// ═══════════════════════════════════════════════════════════════

vec3 flowLines(vec2 p, float fDist, float time, float bassEnv, float midsEnv,
               float trebleEnv, vec3 palPrimary, vec3 palGlow, int lineCount) {
    vec3 col = vec3(0.0);

    // Angle from logo's approximate center of mass
    vec2 logoMid = LOGO_MID;
    float pAngle = atan(p.y - logoMid.y, p.x - logoMid.x);

    for (int i = 0; i < lineCount && i < 10; i++) {
        float fi = float(i);
        float orbitDist = 0.02 + fi * 0.012;
        float speed = 0.4 + fi * 0.1 + trebleEnv * 0.3;
        float dir = (i % 2 == 0) ? 1.0 : -1.0;

        // Distance from the SDF iso-surface at this orbit distance
        float shellDist = abs(fDist - orbitDist);

        // Head angle sweeps around
        float headAngle = time * speed * dir + fi * TAU / float(max(lineCount, 1));
        float angleDiff = mod(pAngle - headAngle + PI, TAU) - PI;

        // Trail mask: comet tail behind the head
        float trailLen = 0.8 + midsEnv * 0.3;
        float trailMask = smoothstep(trailLen, 0.0, angleDiff * dir)
                        * smoothstep(-0.1, 0.0, angleDiff * dir);

        // Line width with bass thickening
        float lineWidth = 0.006 + bassEnv * 0.003;
        float lineMask = smoothstep(lineWidth, lineWidth * 0.2, shellDist);

        float flow = lineMask * trailMask;

        // Bright head, fading tail — bass flares brightness
        float headness = smoothstep(trailLen * 0.3, 0.0, abs(angleDiff));
        vec3 lineCol = mix(palPrimary, palGlow, headness);
        col += lineCol * flow * 0.4 * (1.0 + bassEnv * 0.5);
    }

    return col;
}


// ═══════════════════════════════════════════════════════════════
//  PER-INSTANCE UV COMPUTATION (adapted for Fedora logo)
// ═══════════════════════════════════════════════════════════════

vec2 computeInstanceUV(int idx, int totalCount, vec2 globalUV, float aspect, float time,
                       float logoScale, float bassEnv, float logoPulse,
                       float sizeMin, float sizeMax, out float instScale) {
    vec2 uv = globalUV;
    uv.x = (uv.x - 0.5) * aspect + 0.5;

    if (totalCount <= 1) {
        vec2 drift = vec2(
            sin(time * 0.13) * 0.015 + sin(time * 0.29) * 0.008,
            cos(time * 0.19) * 0.012 + cos(time * 0.11) * 0.006
        );
        uv -= drift;
        // Gentle rotation
        float rotAng = sin(time * 0.12) * 0.04;
        vec2 lp = uv - vec2(0.5);
        uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
                   lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);
        float breathe = 1.0 + sin(time * 0.6) * 0.02;
        float springT = fract(time * 1.2);
        float spring = 1.0 + bassEnv * 0.12 * exp(-springT * 5.0) * cos(springT * 18.0);
        instScale = logoScale * breathe * spring;
        uv = (uv - 0.5) / instScale + LOGO_CENTER;
        return uv;
    }

    float h1 = hash21(vec2(float(idx) * 7.31, 3.17));
    float h2 = hash21(vec2(float(idx) * 13.71, 7.23));
    float h3 = hash21(vec2(float(idx) * 5.13, 11.37));
    float h4 = hash21(vec2(float(idx) * 9.77, 17.53));

    float roam = 0.35;
    float f1 = 0.06 + float(idx) * 0.021;
    float f2 = 0.04 + float(idx) * 0.017;
    vec2 mdrift = vec2(
        sin(time * f1 + h1 * TAU) * roam + sin(time * f1 * 2.1 + h3 * TAU) * roam * 0.3,
        cos(time * f2 + h2 * TAU) * roam * 0.9 + cos(time * f2 * 1.6 + h4 * TAU) * roam * 0.25
    );
    uv -= mdrift;

    // Minimal per-instance tilt (keep "f" essentially upright)
    float rotAng = sin(time * (0.08 + float(idx) * 0.025) + h4 * TAU) * 0.01;
    vec2 lp = uv - vec2(0.5);
    uv = vec2(lp.x * cos(rotAng) - lp.y * sin(rotAng),
               lp.x * sin(rotAng) + lp.y * cos(rotAng)) + vec2(0.5);

    instScale = mix(sizeMin, sizeMax, h3) * logoScale;
    float breathe = 1.0 + sin(time * (0.5 + float(idx) * 0.11) + h1 * TAU) * 0.02;
    float springT = fract(time * 1.2 + h2);
    float spring = 1.0 + bassEnv * 0.12 * exp(-springT * 5.0) * cos(springT * 18.0);
    instScale *= breathe * spring;
    uv = (uv - 0.5) / instScale + LOGO_CENTER;
    return uv;
}


// ═══════════════════════════════════════════════════════════════
//  MAIN ZONE RENDER
// ═══════════════════════════════════════════════════════════════

vec4 renderFedoraZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params,
                      bool isHighlighted, float bass, float mids, float treble,
                      bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);

    // ── Read customParams slots (must match metadata.json) ──────
    // Slots 0-3: customParams[0].xyzw
    float speed         = customParams[0].x >= 0.0 ? customParams[0].x : 0.12;
    float flowSpeed     = customParams[0].y >= 0.0 ? customParams[0].y : 0.25;
    float noiseScale    = customParams[0].z >= 0.0 ? customParams[0].z : 3.5;
    int octaves         = int(customParams[0].w >= 0.0 ? customParams[0].w : 6.0);

    // Slots 4-7: customParams[1].xyzw
    float gridScale     = customParams[1].x >= 0.0 ? customParams[1].x : 4.0;
    float gridStrength  = customParams[1].y >= 0.0 ? customParams[1].y : 0.3;
    float brightness    = customParams[1].z >= 0.0 ? customParams[1].z : 0.8;
    float contrast      = customParams[1].w >= 0.0 ? customParams[1].w : 0.9;

    // Slots 8-11: customParams[2].xyzw
    float fillOpacity       = customParams[2].x >= 0.0 ? customParams[2].x : 0.85;
    float borderGlow        = customParams[2].y >= 0.0 ? customParams[2].y : 0.35;
    float edgeFadeStart     = customParams[2].z >= 0.0 ? customParams[2].z : 30.0;
    float borderBrightness  = customParams[2].w >= 0.0 ? customParams[2].w : 1.4;

    // Slots 12-15: customParams[3].xyzw
    float audioReact    = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;
    float particleStr   = customParams[3].y >= 0.0 ? customParams[3].y : 0.5;
    float innerGlowStr  = customParams[3].z >= 0.0 ? customParams[3].z : 0.45;
    float sparkleStr    = customParams[3].w >= 0.0 ? customParams[3].w : 2.0;

    // Slot 19: customParams[4].w
    float fbmRot        = customParams[4].w >= 0.0 ? customParams[4].w : 0.6;

    // Slots 20-23: customParams[5].xyzw
    float flowDirection = customParams[5].x >= 0.0 ? customParams[5].x : 0.3;
    float logoScale     = customParams[5].y >= 0.0 ? customParams[5].y : 0.5;
    float logoIntensity = customParams[5].z >= 0.0 ? customParams[5].z : 0.85;
    float logoPulse     = customParams[5].w >= 0.0 ? customParams[5].w : 0.8;

    // Slots 24-28: customParams[6].xyzw + customParams[7].x
    int   logoCount     = clamp(int(customParams[6].x >= 0.0 ? customParams[6].x : 4.0), 1, 8);
    float logoSizeMin   = customParams[6].y >= 0.0 ? customParams[6].y : 0.4;
    float logoSizeMax   = customParams[6].z >= 0.0 ? customParams[6].z : 1.0;
    float flowCenterX   = customParams[6].w >= -1.5 ? customParams[6].w : 0.5;
    float flowCenterY   = customParams[7].x >= -1.5 ? customParams[7].x : 0.5;

    // Slot 30: customParams[7].z, Slot 31: customParams[7].w
    float logoSpin      = customParams[7].z >= 0.0 ? customParams[7].z : 0.15;
    float idleStrength  = customParams[7].w >= 0.0 ? customParams[7].w : 0.5;

    // ── Zone geometry ───────────────────────────────────────────
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;

    vec2 p = fragCoord - center;
    float d = sdRoundedBox(p, halfSize, borderRadius);
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    float time = iTime;

    // ── Palette from customColors ───────────────────────────────
    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, FROST_BLUE);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, FROST_DEEP);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, FROST_ICE);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, FROST_SILVER);

    float vitality = isHighlighted ? 1.0 : 0.3;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(time * 0.8 * PI)) * idleStrength;

    float flowAngle = flowDirection * TAU;
    vec2 flowDir = vec2(cos(flowAngle), sin(flowAngle));

    // ── Audio envelopes ─────────────────────────────────────────
    float bassEnv   = hasAudio ? smoothstep(0.02, 0.3, bass) * audioReact : 0.0;
    float midsEnv   = hasAudio ? smoothstep(0.02, 0.4, mids) * audioReact : 0.0;
    float trebleEnv = hasAudio ? smoothstep(0.05, 0.5, treble) * audioReact : 0.0;

    vec2 centeredUV = (globalUV * 2.0 - 1.0) * noiseScale;
    centeredUV.x *= aspect;

    vec4 result = vec4(0.0);

    // ═════════════════════════════════════════════════════════════
    //  INTERIOR FILL
    // ═════════════════════════════════════════════════════════════

    if (d < 0.0) {
        float audioColorShift = midsEnv * 0.15;

        // ── Background: deep navy-to-Fedora-blue gradient ───────
        // Mids shift the gradient position for subtle palette warping
        float skyT = globalUV.y + audioColorShift;
        vec3 skyBottom = mix(FROST_DEEP * 0.6, palSecondary, 0.5);
        vec3 skyTop = mix(FROST_DEEP * 0.4, palSecondary * 0.5, 0.3);
        vec3 col = mix(skyBottom, skyTop, skyT);

        // flowCenter creates radial pull on data streams
        vec2 flowCenter = (vec2(flowCenterX, flowCenterY) * 2.0 - 1.0) * noiseScale;
        flowCenter.x *= aspect;
        vec2 toLogo = flowCenter - centeredUV;
        float pullStrength = 0.15 / (length(toLogo) + 0.1);

        // Subtle FBM cloud wisp overlay for texture variation
        // Bass adds turbulence; mids warp the rotation angle
        vec2 cloudUV = centeredUV * 0.5
                     + (flowDir * flowSpeed + normalize(toLogo + 0.001) * pullStrength) * time * 0.2;
        cloudUV += vec2(sin(time * 2.0), cos(time * 1.5)) * bassEnv * 0.1;
        float clouds = fbm(cloudUV, max(octaves - 2, 3), fbmRot + midsEnv * 0.2);
        vec3 cloudTint = mix(palSecondary, FROST_DEEP, clouds);
        col = mix(col, cloudTint, clouds * 0.25);
        col *= brightness * 0.9;

        // ── Data stream lines ───────────────────────────────────
        col += dataStreams(globalUV, time * speed * 8.0, aspect, bassEnv, midsEnv, trebleEnv,
                          palPrimary, palAccent, palGlow) * 0.6;

        // ── Dot matrix grid overlay ─────────────────────────────
        vec3 dots = dotMatrix(centeredUV + time * speed * 0.1, gridScale, time, bassEnv, midsEnv, trebleEnv,
                              palPrimary * 0.4, palAccent * 0.3);
        col += dots * gridStrength;

        // ── Multi-instance logo rendering ───────────────────────
        for (int li = 0; li < logoCount && li < 8; li++) {
            float instScale;
            vec2 iLogoUV = computeInstanceUV(li, logoCount, globalUV, aspect, time,
                                              logoScale, bassEnv, logoPulse,
                                              logoSizeMin, logoSizeMax, instScale);

            // Wide bounding check
            if (iLogoUV.x < -0.4 || iLogoUV.x > 1.4 ||
                iLogoUV.y < -0.4 || iLogoUV.y > 1.4) continue;

            float maxScale = logoSizeMax * logoScale;
            float depthFactor = clamp(instScale / max(maxScale, 0.01), 0.0, 1.0);
            float instIntensity = logoIntensity * (0.3 + 0.7 * depthFactor);

            // Logo-local coordinates (relative to LOGO_CENTER)
            vec2 logoP = iLogoUV - LOGO_CENTER;
            float logoR = length(logoP);

            // Circular vignette for the logo area
            float logoVignette = 1.0 - smoothstep(0.35, 0.55, logoR);

            // Gentle oscillation only — the "f" must stay upright to be recognizable
            float wobble = sin(time * logoSpin * 2.0 + float(li) * 2.0) * 0.03;
            float cs = cos(wobble), sn = sin(wobble);
            vec2 rotP = vec2(logoP.x * cs - logoP.y * sn, logoP.x * sn + logoP.y * cs);

            float ringBreath = 1.0 + midsEnv * 0.3;

            // Compute SDF
            float fDist = fedoraSDF(rotP, ringBreath);

            // Skip if too far from logo (optimization)
            if (fDist > 0.15) continue;

            vec3 logoCol = vec3(0.0);

            // Subtle flicker for overall logo pulse
            float flicker = neonFlicker(time, float(li) * 3.7, trebleEnv);

            // ── FROSTED GLASS FILL (Effect 2) ───────────────────
            if (fDist < 0.0) {
                // Inside the logo: frosted glass volumetric fill
                float interiorDepth = clamp(-fDist / 0.05, 0.0, 1.0);

                // Subsurface scattering: brighter center, saturated edges
                float sss = interiorDepth * 0.6 + 0.4;

                // Crystalline grain texture (high-freq noise on rotated coords)
                float grain = noise(rotP * 50.0 + time * 0.3) * 0.08;

                // Fresnel rim glow at edges
                float rim = pow(1.0 - interiorDepth, 2.0) * 0.5;

                // Interior color: gradient from primary at edge to glow at center
                vec3 innerCol = mix(palPrimary, palGlow, interiorDepth * 0.6);
                innerCol *= sss + grain;
                innerCol += palGlow * rim * (1.0 + bassEnv * 0.4);

                // Bass pulse on the glass fill
                innerCol *= 1.0 + bassEnv * logoPulse * 0.3;

                // Apply flicker subtly
                innerCol *= 0.85 + 0.15 * flicker;

                // Anti-aliased edge
                float aa = smoothstep(TUBE_HW * 0.1, -TUBE_HW * 0.1, fDist);
                logoCol = mix(logoCol, innerCol * instIntensity, aa);
            }

            // ── SUBTLE OUTER EDGE GLOW (not neon tubes) ─────────
            if (fDist > 0.0 && fDist < 0.05) {
                float edgeGlow = exp(-fDist * 20.0) * 0.4;
                logoCol += palPrimary * edgeGlow * instIntensity * flicker;
            }

            // ── FLOW LINES (Effect 5) ───────────────────────────
            // Flow lines orbit near the SDF surface (orbitDist 0.02–0.13).
            // Deep inside the logo (fDist << 0) they're invisible — skip.
            if (fDist > -0.02) {
                int lineCount = 6 + int(depthFactor * 4.0);
                logoCol += flowLines(rotP, fDist, time + float(li) * 3.0,
                                     bassEnv, midsEnv, trebleEnv,
                                     palPrimary, palGlow, lineCount)
                           * instIntensity * depthFactor;
            }

            // ── Treble sparkle near logo surface ────────────────
            if (trebleEnv > 0.01 && abs(fDist) < 0.03) {
                float sparkN = noise(iLogoUV * 40.0 + time * 6.0 + float(li) * 29.0);
                sparkN = smoothstep(0.55, 0.95, sparkN);
                float edgeMask = smoothstep(0.03, 0.0, abs(fDist));
                logoCol += palGlow * sparkN * edgeMask * trebleEnv * sparkleStr * 0.3 * depthFactor;
            }

            // ── Reinhard tonemap before compositing ─────────────
            logoCol = logoCol / (1.0 + logoCol);

            // Apply circular vignette
            col += logoCol * logoVignette;

        } // end logo instance loop

        // ── Network node particles (Effect 3) ───────────────────
        col += networkNodes(globalUV, time, bassEnv, midsEnv, trebleEnv, particleStr,
                            palPrimary * 0.5, palAccent * 0.4);

        // ── Vitality ────────────────────────────────────────────
        if (isHighlighted) {
            col *= 1.15;
        } else {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.15);
            col *= 0.75 + idlePulse * 0.15;
        }

        // ── PULSE RING BORDER (Effect 6) ────────────────────────
        float innerDist = -d;
        float vignette = smoothstep(0.0, edgeFadeStart, innerDist);
        col *= mix(0.7, 1.0, vignette);

        // Subtle inner glow
        col += palPrimary * exp(-innerDist / 10.0) * innerGlowStr * 0.5;

        // Periodic pulse ring sweeping inward (every ~4 seconds)
        float ringPhase = fract(time * 0.25);
        float ringPos = ringPhase * 60.0;
        float ring = exp(-(innerDist - ringPos) * (innerDist - ringPos) * 0.05) * (1.0 - ringPhase);

        // Bass-triggered extra ring
        float bassRingPhase = fract(time * 0.7);
        float bassRingPos = bassRingPhase * 42.0;
        float bassRing = exp(-(innerDist - bassRingPos) * (innerDist - bassRingPos) * 0.08) * bassEnv * (1.0 - bassRingPhase);

        col += palAccent * (ring * 0.3 + bassRing * 0.5);

        // Tint with fill color
        col = mix(col, fillColor.rgb * luminance(col), 0.1);

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // ── Border ──────────────────────────────────────────────────
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float angle = atan(p.y, p.x) * 2.0;
        // FBM-animated border with frost colors
        float borderFlow = fbm(vec2(sin(angle), cos(angle)) * 2.0 + time * speed * 3.0, 3, 0.5);
        vec3 borderCol = frostPalette(borderFlow * contrast + midsEnv * 0.2,
                                      palPrimary, palSecondary, palAccent);
        vec3 zoneBorderTint = colorWithFallback(borderColor.rgb, borderCol);
        borderCol = mix(borderCol, zoneBorderTint * luminance(borderCol), 0.25);
        borderCol *= borderBrightness;

        // Soft glow along border
        float borderSoftGlow = smoothstep(borderWidth * 2.0, 0.0, abs(d)) * 0.2;
        borderCol += palAccent * borderSoftGlow;

        if (isHighlighted) {
            float bBreathe = 0.85 + 0.15 * sin(time * 2.5);
            float borderBass = hasAudio ? 1.0 + bassEnv * 0.4 : 1.0;
            borderCol *= bBreathe * borderBass;
        } else {
            float lum = luminance(borderCol);
            borderCol = mix(borderCol, vec3(lum), 0.15);
            borderCol *= 0.7;
        }

        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }

    // ── Outer glow ──────────────────────────────────────────────
    float bassGlowPush = hasAudio ? bassEnv * 2.5 : idlePulse * 5.0;
    float glowRadius = mix(10.0, 20.0, vitality) + bassGlowPush;
    if (d > 0.0 && d < glowRadius && borderGlow > 0.01) {
        float glow = expGlow(d, 8.0, borderGlow);
        float oAngle = atan(p.y, p.x);
        float glowT = angularNoise(oAngle, 1.5, time * 0.06) + midsEnv * 0.1;
        vec3 glowCol = frostPalette(glowT, palPrimary, palSecondary, palAccent);
        glowCol *= mix(0.3, 1.0, vitality);
        result.rgb += glowCol * glow * 0.8;
        result.a = max(result.a, glow * 0.6);
    }

    return result;
}


// ═══════════════════════════════════════════════════════════════
//  LABEL COMPOSITING (Frosted variant)
// ═══════════════════════════════════════════════════════════════

vec4 compositeFedoraLabels(vec4 color, vec2 fragCoord,
                           float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 palPrimary   = colorWithFallback(customColors[0].rgb, FROST_BLUE);
    vec3 palSecondary = colorWithFallback(customColors[1].rgb, FROST_DEEP);
    vec3 palAccent    = colorWithFallback(customColors[2].rgb, FROST_ICE);
    vec3 palGlow      = colorWithFallback(customColors[3].rgb, FROST_SILVER);

    float labelGlowSpread = customParams[4].x >= 0.0 ? customParams[4].x : 3.0;
    float labelBrightness = customParams[4].y >= 0.0 ? customParams[4].y : 2.5;
    float labelAudioReact = customParams[4].z >= 0.0 ? customParams[4].z : 1.0;

    float time = iTime;

    // ── Audio envelopes (noise-gated, matching main effect quality) ──
    float bassEnv   = hasAudio ? smoothstep(0.02, 0.3, bass)   * labelAudioReact : 0.0;
    float midsEnv   = hasAudio ? smoothstep(0.02, 0.4, mids)   * labelAudioReact : 0.0;
    float trebleEnv = hasAudio ? smoothstep(0.05, 0.5, treble) * labelAudioReact : 0.0;

    // ── Multi-layer frost halo ──────────────────────────────────
    float haloTight = 0.0;
    float haloWide = 0.0;
    float haloVWide = 0.0;
    float haloR = 0.0, haloG = 0.0, haloB = 0.0;
    vec2 chromOff = vec2(px.x * 2.0, px.y * 0.6);

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
    haloTight /= 10.0;
    haloWide /= 16.5;
    haloVWide /= 20.0;
    haloR /= 16.5;
    haloG /= 16.5;
    haloB /= 16.5;

    // ── Gentle pulse (not aggressive neon flicker) ──────────────
    float pulse = 0.92 + 0.08 * sin(time * 3.0);

    // ── Color sweep across text — mids modulate sweep speed ─────
    float sweep = fract(time * (0.12 + midsEnv * 0.08));
    float sweepPos = uv.x * 0.7 + uv.y * 0.3;
    float sweepWave = smoothstep(sweep - 0.3, sweep, sweepPos) *
                      smoothstep(sweep + 0.3, sweep, sweepPos);
    float sweepBright = 1.0 + sweepWave * 0.4;

    if (haloWide > 0.003) {
        float haloEdge = haloWide * (1.0 - labels.a);
        float haloEdgeTight = haloTight * (1.0 - labels.a);
        float haloEdgeVWide = haloVWide * (1.0 - labels.a);

        // Animated color cycling through frost palette
        float t = uv.x * 2.0 + time * 0.10;
        vec3 haloCol = frostPalette(t, palPrimary, palAccent, palGlow);

        // Tight core: near-white
        vec3 coreCol = mix(FROST_SILVER, haloCol, 0.25);
        color.rgb += coreCol * haloEdgeTight * 0.7 * pulse * sweepBright;

        // Chromatic bloom
        vec3 chromHalo = vec3(haloR, haloG, haloB) * (1.0 - labels.a);
        vec3 chromCol = chromHalo * haloCol * 0.5 * pulse;
        color.rgb += chromCol;

        // Wide ambient haze
        color.rgb += haloCol * 0.35 * haloEdgeVWide * pulse * (0.7 + bassEnv * 0.4);

        // Treble sparks along halo edge
        if (trebleEnv > 0.04) {
            float sparkNoise = noise2D(uv * 50.0 + time * 5.0);
            float spark = smoothstep(0.6, 0.92, sparkNoise) * trebleEnv * 2.0;
            color.rgb += palGlow * haloEdge * spark * pulse;
        }

        color.a = max(color.a, haloEdge * 0.6);
    }

    // ── Label text body: frosted glass text ─────────────────────
    if (labels.a > 0.01) {
        // Diagonal frost gradient in pixel space — visible within each character
        float frostWave = sin(fragCoord.x * 0.25 - time * 2.0 + fragCoord.y * 0.15) * 0.5 + 0.5;
        vec3 tubeColor = frostPalette(frostWave + time * 0.06, palPrimary, palAccent, palGlow);

        // Frost crystalline noise — breaks up the solid fill
        float frost = noise2D(fragCoord * 0.12 + time * 0.5);
        vec3 frostTint = mix(tubeColor, FROST_SILVER, frost * 0.5);

        // Stroke edge detection for frosted rim
        float aL = texture(uZoneLabels, uv + vec2(-px.x, 0.0)).a;
        float aR = texture(uZoneLabels, uv + vec2( px.x, 0.0)).a;
        float aU = texture(uZoneLabels, uv + vec2(0.0, -px.y)).a;
        float aD = texture(uZoneLabels, uv + vec2(0.0,  px.y)).a;
        float rim = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);

        // Combine: frost body + bright silver rim at edges
        vec3 textCol = frostTint * 0.7 + FROST_SILVER * rim * 0.6;
        textCol *= labelBrightness * pulse * sweepBright;
        textCol *= 1.0 + bassEnv * 0.5;

        // Gentle tonemap preserving frost color variation
        textCol = textCol / (0.6 + textCol);

        color.rgb = mix(color.rgb, textCol, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}


// ═══════════════════════════════════════════════════════════════
//  ENTRY POINT
// ═══════════════════════════════════════════════════════════════

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

        vec4 zoneColor = renderFedoraZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    // Slot 29 (showLabels): customParams[7].y
    if (customParams[7].y > 0.5) {
        color = compositeFedoraLabels(color, fragCoord, bass, mids, treble, hasAudio);
    }

    fragColor = clampFragColor(color);
}
