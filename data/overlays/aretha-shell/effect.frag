// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Aretha Shell — Ghost in the Shell / Aretha Dark Layered Shader
// Ported from Ghostty shader: ~/.config/ghostty/shaders/aretha_shell.glsl
// Original: MonoBall-inspired three-layer composite (Color Grade + Hex Grid + Data Streams)
// PlasmaZones enhancements: audio reactivity, bidirectional streams, IDS scan lines

#include <audio.glsl>


// ═══════════════════════════════════════════════════════════════════════════════
// PARAMETERS  (declared in metadata.json, read via the generated p_<id>
// accessors below; the slot-range note that used to sit here counted only
// the parameters the pack had at the time and drifted as more were added)
// ═══════════════════════════════════════════════════════════════════════════════

float getSpeed()           { return p_speed >= 0.0 ? p_speed : 0.08; }
float getGradeIntensity()  { return p_gradeIntensity >= 0.0 ? p_gradeIntensity : 0.25; }
float getGradeSaturation() { return p_gradeSaturation >= 0.0 ? p_gradeSaturation : 1.08; }
float getShimmerIntensity(){ return p_shimmerIntensity >= 0.0 ? p_shimmerIntensity : 0.02; }
float getHexPixelSize()    { return p_hexPixelSize >= 0.0 ? p_hexPixelSize : 35.0; }
float getHexLineThickness(){ return p_hexLineThickness >= 0.0 ? p_hexLineThickness : 0.05; }
float getHexOpacity()      { return p_hexOpacity >= 0.0 ? p_hexOpacity : 0.18; }
float getHexPulseSpeed()   { return p_hexPulseSpeed >= 0.0 ? p_hexPulseSpeed : 1.5; }
float getHexScanSpeed()    { return p_hexScanSpeed >= 0.0 ? p_hexScanSpeed : 1.0; }
float getStreamColumns()   { return p_streamColumns >= 0.0 ? p_streamColumns : 35.0; }
float getStreamOpacity()   { return p_streamOpacity >= 0.0 ? p_streamOpacity : 0.15; }
float getStreamSpeed()     { return p_streamSpeed >= 0.0 ? p_streamSpeed : 0.8; }
float getTrailLength()     { return p_trailLength >= 0.0 ? p_trailLength : 0.20; }
float getAudioSensitivity(){ return p_audioSensitivity >= 0.0 ? p_audioSensitivity : 1.0; }
float getFillOpacity()     { return p_fillOpacity >= 0.0 ? p_fillOpacity : 0.95; }
float getDataSurgeIntensity() { return p_dataSurgeIntensity >= 0.0 ? p_dataSurgeIntensity : 1.0; }

// Tri-Hex parameters
float getTriLineThickness() { return p_triLineThickness >= 0.0 ? p_triLineThickness : 0.03; }
float getTriLineOpacity()   { return p_triLineOpacity >= 0.0 ? p_triLineOpacity : 0.25; }
float getTriFillOpacity()   { return p_triFillOpacity >= 0.0 ? p_triFillOpacity : 0.12; }
float getTriPulseSpeed()    { return p_triPulseSpeed >= 0.0 ? p_triPulseSpeed : 2.0;  }
float getTriRotSpeed()      { return p_triRotSpeed >= 0.0 ? p_triRotSpeed : 0.4;  }
float getTriColorMix()      { return p_triColorMix >= 0.0 ? p_triColorMix : 0.5;  }

// ═══════════════════════════════════════════════════════════════════════════════
// COLORS
// ═══════════════════════════════════════════════════════════════════════════════

const vec3 arethaBg       = vec3(0.098, 0.153, 0.259);  // #192742
const vec3 arethaLavender = vec3(0.482, 0.388, 0.776);  // #7b63c6

const vec3 gradeShadow    = vec3(0.098, 0.153, 0.259);
const vec3 gradeMid       = vec3(0.000, 0.620, 0.741);
const vec3 gradeHighlight = vec3(0.333, 0.667, 1.000);

vec3 getBackgroundColor() {
    vec3 bg = p_backgroundColor.rgb;
    return length(bg) > 0.01 ? bg : arethaBg;
}

vec3 getArethaCyan()   { vec3 c = p_arethaCyan.rgb; return length(c) > 0.01 ? c : vec3(0.333, 0.667, 1.000); }
vec3 getArethaPink()   { vec3 c = p_arethaPink.rgb; return length(c) > 0.01 ? c : vec3(1.000, 0.333, 0.498); }
vec3 getArethaTeal()   { vec3 c = p_arethaTeal.rgb; return length(c) > 0.01 ? c : vec3(0.000, 0.620, 0.741); }
vec3 getArethaPurple() { vec3 c = p_arethaPurple.rgb; return length(c) > 0.01 ? c : vec3(0.333, 0.333, 1.000); }

// Tri-Hex colors (warm accents to contrast with cool hex grid)
vec3 getTriColor1()    { vec3 c = p_triColor1.rgb; return length(c) > 0.01 ? c : vec3(1.000, 0.200, 0.550); }  // #ff3388 hot pink
vec3 getTriColor2()    { vec3 c = p_triColor2.rgb; return length(c) > 0.01 ? c : vec3(1.000, 0.450, 0.300); }  // #ff734d warm coral
vec3 getTriLineColor() { vec3 c = p_triLineColor.rgb; return length(c) > 0.01 ? c : vec3(0.900, 0.150, 0.700); }  // #e626b3 vivid magenta

// ═══════════════════════════════════════════════════════════════════════════════
// UTILITIES
// ═══════════════════════════════════════════════════════════════════════════════


// ═══════════════════════════════════════════════════════════════════════════════
// LAYER 1: NEON COLOR GRADE  (from original, unchanged)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 colorGrade(vec3 color) {
    float GRADE_INTENSITY = getGradeIntensity();
    float GRADE_SATURATION = getGradeSaturation();
    float lum = luminance(color);

    vec3 graded;
    if (lum < 0.33) {
        graded = mix(gradeShadow, gradeMid, lum / 0.33);
    } else if (lum < 0.66) {
        graded = mix(gradeMid, gradeHighlight, (lum - 0.33) / 0.33);
    } else {
        graded = mix(gradeHighlight, vec3(1.0), (lum - 0.66) / 0.34);
    }

    vec3 result = mix(color, graded * (color + 0.1), GRADE_INTENSITY);
    float gray = luminance(result);
    result = mix(vec3(gray), result, GRADE_SATURATION);
    return result;
}

float shimmer(vec2 uv, float t, float lum) {
    if (lum < 0.5) return 0.0;
    float s = sin(t * 3.0 + uv.x * 12.0 + uv.y * 8.0) * 0.5 + 0.5;
    return getShimmerIntensity() * s * (lum - 0.5) * 2.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LAYER 2: HEX GRID  (original ambient base + gentle audio overlay)
// ═══════════════════════════════════════════════════════════════════════════════

float hexDist(vec2 p) {
    p = abs(p);
    return max(p.x * 0.866025 + p.y * 0.5, p.y);
}

vec2 hexCoord(vec2 uv) {
    vec2 r = vec2(1.0, 1.732);
    vec2 h = r * 0.5;
    vec2 a = mod(uv, r) - h;
    vec2 b = mod(uv - h, r) - h;
    return dot(a, a) < dot(b, b) ? a : b;
}

// ═══════════════════════════════════════════════════════════════════════════════
// TRI-HEX OVERLAY  (triangular regions between hexagons at vertex junctions)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 triHexOverlay(vec2 hex, float d, vec2 hexCenter, float t,
                   float bass, float treble) {
    float triLineThick = getTriLineThickness();
    float triLineAlpha = getTriLineOpacity();
    float triFillAlpha = getTriFillOpacity();
    float triPulseSpd  = getTriPulseSpeed();
    float triRotSpd    = getTriRotSpeed();
    float triColMix    = getTriColorMix();
    float audioSens    = getAudioSensitivity();

    vec3 triColor1 = getTriColor1();
    vec3 triColor2 = getTriColor2();
    vec3 triLineC  = getTriLineColor();

    bool hasAudio = iAudioSpectrumSize > 0;
    vec3 result = vec3(0.0);

    // ── Identify triangular regions at hex vertices ─────────────────────────
    // Hex vertices sit at 0°, 60°, 120°, 180°, 240°, 300° from cell center.
    // Where 3 hexagons meet, the corner patches form triangles.
    float angle = atan(hex.y, hex.x);

    // Angular distance to nearest vertex direction
    float va = mod(angle + PI / 6.0 + 200.0 * PI, PI / 3.0);  // 0..PI/3, ~0 at vertex angles
    float vDist = min(va, PI / 3.0 - va);               // 0 at vertex, PI/6 at edge midpoint

    // Which vertex (0-5)
    int vIdx = int(mod(floor((angle + 200.0 * PI + PI / 6.0) / (PI / 3.0)), 6.0));

    // Triangle mask: outer ring of hex cell AND near a vertex direction
    // Tight ramps give near-binary fill (no visible gradient bands inside triangle)
    float outer   = smoothstep(0.33, 0.36, d);                            // sharp at inner boundary
    float angMask = 1.0 - smoothstep(PI / 7.0 - 0.08, PI / 7.0, vDist);  // sharp at angular boundary
    float triMask = outer * angMask;

    if (triMask < 0.001) return result;  // early out

    // ── Per-triangle identity (consistent across the 3 cells sharing a vertex) ──
    float vAngle = float(vIdx) * PI / 3.0;
    vec2 vPos = hexCenter + vec2(cos(vAngle), sin(vAngle)) / sqrt(3.0);
    vec2 triGridId = floor(vPos * 1.74 + 0.5);  // quantize to vertex grid
    float triSeed  = hash21(triGridId);
    float triPhase = triSeed * TAU;

    // ── Animated fill ───────────────────────────────────────────────────────
    float rotOffset = t * triRotSpd;
    if (hasAudio) {
        // Discrete step rotation: bass snaps wedges in 60-degree increments (loading indicator)
        float stepCount = floor(bass * audioSens * 6.0);  // 0-6 steps at max bass
        rotOffset += stepCount * (PI / 3.0);
    }

    // Pulse per triangle — pow sharpens to brief flashes (like hex scan's pow(8))
    float pulse = sin(t * triPulseSpd + triPhase + rotOffset);
    pulse = pulse * 0.5 + 0.5;
    pulse = pow(pulse, 5.0);  // mostly dim, brief bright peaks

    float fill = triMask * pulse;

    // Audio: bass triggers per-triangle corruption (cells randomly flip on bass hits)
    if (hasAudio) {
        float glitchTrigger = step(0.7, bass) * step(triSeed, 0.3 + bass * 0.4);
        fill = mix(fill, 1.0 - fill, glitchTrigger * audioSens);  // invert fill on trigger
    }
    if (hasAudio && treble > 0.3) {
        float flash = hash21(triGridId + vec2(0.0, floor(t * 2.0)));
        if (flash > 0.92) {
            fill += triMask * treble * audioSens * 0.15;
        }
    }

    // Color: alternate triangles between triColor1 and triColor2
    vec3 fillCol = (vIdx % 2 == 0) ? triColor1 : triColor2;
    fillCol = mix(triColor1, fillCol, triColMix);
    result += fillCol * fill * triFillAlpha;

    // ── Triangle edge lines ─────────────────────────────────────────────────
    // Inner edge (boundary facing hex center)
    float innerEdge = smoothstep(triLineThick, 0.0, abs(d - 0.36) * 2.5);
    innerEdge *= angMask;
    // Angular boundary edges (where triangle meets non-triangle region)
    float angEdge = smoothstep(triLineThick * 0.5, 0.0, abs(vDist - PI / 7.0) * length(hex) * 1.5);
    angEdge *= outer;
    result += triLineC * max(innerEdge, angEdge) * triLineAlpha;

    return result;
}

vec3 hexGrid(vec2 pixelCoord, float t, vec2 screenUV,
             float bass, float mids, float treble, float overall) {
    float HEX_PIXEL_SIZE    = getHexPixelSize();
    float HEX_LINE_THICKNESS = getHexLineThickness();
    float HEX_OPACITY       = getHexOpacity();
    float HEX_PULSE_SPEED   = getHexPulseSpeed();
    float HEX_SCAN_SPEED    = getHexScanSpeed();
    float audioSens         = getAudioSensitivity();

    vec3 arethaCyan   = getArethaCyan();
    vec3 arethaPurple = getArethaPurple();
    vec3 arethaTeal   = getArethaTeal();
    vec3 arethaPink   = getArethaPink();

    bool hasAudio = iAudioSpectrumSize > 0;

    // Hex grid in pixel space (size stays constant regardless of zone size)
    // zoneLen(): HEX_PIXEL_SIZE is a logical-px setting divided into a
    // device-px fragCoord, so without it the cells halved in physical size on
    // a 2x display while the border and corner around them kept theirs.
    vec2 scaledUV = pixelCoord / max(zoneLen(HEX_PIXEL_SIZE), 1.0);
    vec2 hex = hexCoord(scaledUV);
    float d = hexDist(hex);

    vec2 hexCenter = scaledUV - hex;
    vec2 hexId = floor(hexCenter / vec2(1.0, 1.732));

    // Hex edges
    float edge = smoothstep(0.5 - HEX_LINE_THICKNESS, 0.5, d);
    edge *= smoothstep(0.5 + HEX_LINE_THICKNESS, 0.5, d);
    edge = 1.0 - edge;

    // ── Original ambient behavior (always active) ─────────────────────────
    // Pulse: gentle sine wave through the grid
    float pulseSpeed = HEX_PULSE_SPEED;
    float pulseAmp = 0.5;

    // Audio: data surge pulses radiate from random origin points on bass hits
    if (hasAudio) {
        // Per-beat origin point (changes every ~0.5s quantized by bass energy)
        float beatSlot = floor(t * 2.0 + bass * 3.0);
        vec2 surgeOrigin = hash22(vec2(beatSlot, 7.31)) * 40.0 - 20.0;  // in hex grid coords
        float surgeDist = length(hexCenter - surgeOrigin);
        float surgeWave = sin(surgeDist * 1.5 - t * pulseSpeed * 4.0) * 0.5 + 0.5;
        surgeWave *= exp(-surgeDist * 0.08);  // fade with distance
        pulseAmp += surgeWave * bass * audioSens * 0.8;
    }

    float pulse = sin(t * pulseSpeed + hexId.x * 0.5 + hexId.y * 0.3) * 0.5 + 0.5;

    // Scan line sweeping down (original behavior)
    float scanSpeed = HEX_SCAN_SPEED;
    if (hasAudio) {
        // Mids cause scan line to stutter/jump rather than uniform speedup
        float scanJitter = step(0.5, mids) * hash11(floor(t * 8.0)) * 2.0;
        scanSpeed += scanJitter * audioSens * 0.8;
    }
    float scan = sin(hexCenter.y * 0.2 - t * scanSpeed) * 0.5 + 0.5;
    scan = pow(scan, 8.0);

    // Grid intensity: original formula
    float gridIntensity = edge * (0.3 + pulse * pulseAmp);
    gridIntensity += scan * 0.25;

    // Audio: hex cell corruption — bass triggers individual cells to glow/glitch
    if (hasAudio) {
        float cellHash = hash21(hexId + vec2(floor(t * 1.5), 0.0));
        float corruptTrigger = step(0.7, bass) * step(cellHash, 0.25 + bass * 0.5);
        float interiorGlow = (1.0 - smoothstep(0.0, 0.45, d)) * corruptTrigger * 0.6;
        gridIntensity += interiorGlow;
        // Network overload: energy drives scan-line interference across the grid
        float scanGlitch = step(0.6, overall) * step(hash11(floor(screenUV.y * 200.0) + floor(t * 8.0)), 0.92);
        gridIntensity += scanGlitch * 0.15;
    }

    // Random hex blinks (original behavior, treble increases rate)
    float blinkRate = 0.3;
    float blinkThreshold = 0.94;
    if (hasAudio) {
        blinkRate += treble * audioSens * 1.0;
        blinkThreshold -= treble * audioSens * 0.06;
    }
    float hexRand = hash21(hexId + floor(t * blinkRate));
    if (hexRand > blinkThreshold) {
        float blink = sin(t * 5.0 + hexRand * 100.0) * 0.5 + 0.5;
        gridIntensity += blink * 0.6;
    }

    // Color: original ambient cycling
    vec3 gridColor = mix(arethaCyan, arethaPurple, sin(t + screenUV.y * 2.0) * 0.5 + 0.5);
    gridColor = mix(gridColor, arethaTeal, pulse * 0.3);

    // Audio: network overload color shift — energy causes per-cell color corruption
    if (hasAudio) {
        float cellId = hash21(hexId);
        float overloadShift = step(0.5, overall) * cellId;
        // Cells with high hash values shift toward pink/hot when energy is high
        gridColor = mix(gridColor, arethaPink, overloadShift * overall * audioSens * 0.4);
    }

    // Tri-hex overlay: animated triangles between hexagons (at vertex junctions)
    vec3 triHex = triHexOverlay(hex, d, hexCenter, t, bass, treble);

    return gridColor * gridIntensity * HEX_OPACITY + triHex;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LAYER 3: DATA STREAMS  (original base + bidirectional + gentle bass influence)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 dataStream(vec2 uv, float t, float bass, float overall) {
    float STREAM_COLUMNS = getStreamColumns();
    float STREAM_OPACITY = getStreamOpacity();
    float STREAM_SPEED   = getStreamSpeed();
    float TRAIL_LENGTH   = getTrailLength();
    float audioSens      = getAudioSensitivity();
    float surgeStr       = getDataSurgeIntensity();

    vec3 arethaCyan = getArethaCyan();
    vec3 arethaTeal = getArethaTeal();
    vec3 arethaPink = getArethaPink();

    bool hasAudio = iAudioSpectrumSize > 0;

    // Column setup (from original)
    float columnWidth = 1.0 / STREAM_COLUMNS;
    float column = floor(uv.x / columnWidth);
    float columnX = fract(uv.x / columnWidth);

    float columnSeed = hash11(column);
    float columnSpeed = 0.5 + columnSeed * 0.8;
    float columnPhase = columnSeed * 100.0;

    // Direction: roughly half go up, half go down (bidirectional digital rain)
    bool goesUp = columnSeed > 0.5;

    // Bass gently speeds up some columns
    float speedMul = 1.0;
    if (hasAudio && bass > 0.05) {
        float surge = bass * audioSens * surgeStr;
        // Surge columns get a modest speed boost (up to ~1.8x at max bass)
        float surgeSeed = hash11(column + floor(t * 0.6) * 7.13);
        if (surgeSeed < 0.3 + surge * 0.4) {
            speedMul = 1.0 + surge * 0.8;
        }
    }

    // Head position
    float rawPos = fract(t * STREAM_SPEED * columnSpeed * speedMul + columnPhase);
    float headY = goesUp ? (1.0 - rawPos) : rawPos;

    // Trail behind the head
    float distFromHead = goesUp ? (uv.y - headY) : (headY - uv.y);
    if (distFromHead < 0.0) distFromHead += 1.0;

    // Trail with burst afterimage: energy causes sharp trail extensions with per-column variation
    float effectiveTrail = TRAIL_LENGTH;
    if (hasAudio) {
        // Squared overall for burst feel — brief sharp extensions, not steady growth
        float burstTrail = overall * overall * audioSens * 0.6;
        // Per-column variation: only some columns get afterimage extension
        float trailSeed = hash11(column + 99.0);
        effectiveTrail *= 1.0 + burstTrail * step(0.4, trailSeed);
    }
    float trail = 1.0 - distFromHead / effectiveTrail;
    trail = max(0.0, trail);
    trail = trail * trail;

    // Column mask (original)
    float columnMask = 1.0 - abs(columnX - 0.5) * 4.0;
    columnMask = max(0.0, columnMask);

    // Character flicker (original)
    float charFlicker = hash11(floor(uv.y * 50.0) + floor(t * 8.0) + column);
    if (trail > 0.1) {
        trail *= 0.7 + charFlicker * 0.3;
    }

    // Bright head (original)
    float head = smoothstep(0.025, 0.0, distFromHead);

    float streamEffect = (trail * 0.5 + head * 1.2) * columnMask;

    // Some columns brighter (original)
    if (columnSeed > 0.7) {
        streamEffect *= 1.4;
    }

    // Column activation: bass lowers threshold, activating more columns
    float activationThreshold = 0.45;
    if (hasAudio) {
        activationThreshold = mix(0.45, 0.20, bass * audioSens * surgeStr);
    }
    float columnActive = step(activationThreshold, hash11(column + floor(t * 0.4)));
    streamEffect *= columnActive;

    // Color (original)
    vec3 streamColor = mix(arethaCyan, arethaTeal, columnSeed);

    // Bass tints fast columns toward pink
    if (hasAudio && speedMul > 1.3) {
        streamColor = mix(streamColor, arethaPink, (speedMul - 1.3) * 0.5);
    }

    // Brighter heads (original, with data-packet flash on bass)
    if (head > 0.5) {
        float headWhiteness = 0.4;
        if (hasAudio) {
            // Data packet flash: bass causes bright heads to overexpose briefly per-column
            float packetFlash = step(0.6, bass) * step(hash11(column + floor(t * 4.0)), 0.7);
            headWhiteness += packetFlash * audioSens * 0.4;
        }
        streamColor = mix(streamColor, vec3(1.0), headWhiteness);
    }

    return streamColor * streamEffect * STREAM_OPACITY;
}

// ═══════════════════════════════════════════════════════════════════════════════
// HORIZONTAL SCAN LINE  (from original, was missing in PZ version)
// ═══════════════════════════════════════════════════════════════════════════════

float scanLine(vec2 uv, float t) {
    float scanY = 1.0 - fract(t * 0.25);
    return smoothstep(0.015, 0.0, abs(uv.y - scanY)) * 0.15;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AMBIENT GLOW  (from original)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 ambientGlow(vec2 uv, float t, float overall) {
    vec3 arethaTeal = getArethaTeal();
    float glow = sin(uv.x * 3.0 + t) * sin(uv.y * 2.0 - t * 0.7) * 0.5 + 0.5;
    float intensity = 0.03;
    // Audio: network overload — energy drives ambient toward overexposed/clipped look
    if (iAudioSpectrumSize > 0) {
        float overload = step(0.4, overall) * (overall - 0.4) * getAudioSensitivity();
        // Spatial variation: glow intensifies at screen edges (data overflow from periphery)
        float edgeDist = length(uv - 0.5) * 2.0;
        intensity += overload * 0.08 * (0.5 + edgeDist);
    }
    return arethaTeal * glow * intensity;
}

// ═══════════════════════════════════════════════════════════════════════════════
// GLITCH  (from original, treble-gated when audio present)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 glitch(vec2 uv, float t, float treble) {
    vec3 arethaCyan   = getArethaCyan();
    vec3 arethaPink   = getArethaPink();
    vec3 arethaPurple = getArethaPurple();

    vec3 glitchColor = vec3(0.0);
    float glitchT = floor(t * 3.0);
    float glitchSeed = hash11(glitchT);

    // Original threshold: 0.98 (~2% of the time)
    // With audio: treble lowers threshold, making glitches more frequent on highs
    float threshold = 0.98;
    if (iAudioSpectrumSize > 0 && treble > 0.15) {
        threshold -= (treble - 0.15) * getAudioSensitivity() * 0.06;
    }

    if (glitchSeed > threshold) {
        float intensity = (glitchSeed - threshold) / (1.0 - threshold);

        float blockSize = 8.0 + hash11(glitchT + 1.0) * 24.0;
        float block = floor(uv.y * blockSize);
        float blockRand = hash21(vec2(block, glitchT));

        if (blockRand > 0.7) {
            float colorShift = hash21(vec2(block + 200.0, glitchT));
            vec3 corruptColor;
            if (colorShift < 0.33) corruptColor = arethaCyan;
            else if (colorShift < 0.66) corruptColor = arethaPink;
            else corruptColor = arethaPurple;

            float scanIntensity = sin(uv.y * 200.0 + t * 50.0) * 0.5 + 0.5;
            scanIntensity = pow(scanIntensity, 4.0);

            glitchColor += corruptColor * 0.08 * intensity;
            glitchColor += corruptColor * scanIntensity * 0.04 * intensity;

            if (blockRand > 0.92) {
                float lineY = fract(uv.y * blockSize);
                if (lineY < 0.06 || lineY > 0.94) {
                    glitchColor += vec3(1.0) * 0.15 * intensity;
                }
            }
        }

        if (glitchSeed > 0.995) {
            float bandY = hash11(glitchT + 50.0);
            float bandDist = abs(uv.y - bandY);
            if (bandDist < 0.02) {
                float bandIntensity = 1.0 - bandDist / 0.02;
                glitchColor += mix(arethaCyan, arethaPink, hash11(glitchT + 60.0)) * bandIntensity * 0.12;
            }
        }
    }

    return glitchColor;
}

// ═══════════════════════════════════════════════════════════════════════════════
// IDS SCAN LINES  (treble-driven enhancement, not in original)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 idsScanLines(vec2 uv, float t, float treble) {
    if (treble < 0.15) return vec3(0.0);

    vec3 arethaCyan = getArethaCyan();
    vec3 arethaPink = getArethaPink();
    float audioSens = getAudioSensitivity();
    vec3 result = vec3(0.0);

    float intensity = smoothstep(0.15, 0.6, treble) * audioSens;
    float sweepSpeed = 2.0 + intensity * 8.0;
    int numLines = 1 + int(intensity * 3.0);

    for (int i = 0; i < 4; i++) {
        if (i >= numLines) break;

        float linePhase = float(i) * 1.618;
        float lineSeed = hash11(float(i) + floor(t * 1.5) * 3.7);

        float sweepX = fract(t * sweepSpeed * (0.6 + lineSeed * 0.4) + linePhase);
        float lineY = fract(lineSeed + sin(t * 0.3 + float(i) * 2.1) * 0.15);

        float bandWidth = 0.004 + 0.003 * lineSeed;
        float band = smoothstep(bandWidth, bandWidth * 0.3, abs(uv.y - lineY));

        float sweepDist = uv.x - sweepX;
        if (sweepDist < 0.0) sweepDist += 1.0;

        float leading = smoothstep(0.06, 0.0, sweepDist);
        float trailing = smoothstep(0.25, 0.0, sweepDist) * 0.3;

        vec3 lineColor = (i % 2 == 0) ? arethaCyan : arethaPink;
        result += lineColor * band * (leading + trailing) * intensity * 1.2;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// VIGNETTE  (from original)
// ═══════════════════════════════════════════════════════════════════════════════

float vignette(vec2 uv) {
    return 1.0 - length(uv - 0.5) * 0.2;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ZONE RENDERING
// ═══════════════════════════════════════════════════════════════════════════════

vec4 renderArethaZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                      vec4 params, bool isHighlighted,
                      float bass, float mids, float treble, float overall, bool hasAudio)
{
    float SPEED = getSpeed();
    float vitality = zoneVitality(isHighlighted);

    // Corner radius: logical px to device px, clamped to half the zone's smaller side.
    // Shared with the decoration side via zoneSdf() in shared/common.glsl.
    ZoneSDF zoneShape = zoneSdf(fragCoord, rect, params.x);
    float borderWidth  = zoneBorderWidth(params.y);


    float d = zoneShape.d;

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        float t = iTime * SPEED;
        vec3 fx = vec3(0.0);

        // Full-screen UV for continuous effects across zones
        vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));

        // Base color
        vec3 baseColor = getBackgroundColor();
        // The pack's own fillOpacity is the sole fill alpha, catalog-wide.
        float bgAlpha = getFillOpacity();
        // The fill COLOUR is separate and was being discarded entirely. Light
        // identity tint at the sibling packs' weight, through zoneFillHue()
        // because zoneFillColors[i].rgb arrives premultiplied.
        baseColor = zoneTint(baseColor, fillColor, 0.35);

        // Layer 1: Color Grade
        vec3 gradedBg = colorGrade(baseColor);
        float gradeStrength = 0.5;
        if (hasAudio) {
            // Network overload: energy pushes color grading toward clipped/overexposed
            // Spatially varied — brighter regions clip harder (cyberpunk data overload)
            float lumBase = luminance(gradedBg);
            float clipPush = step(0.45, overall) * (overall - 0.45) * getAudioSensitivity();
            gradeStrength += clipPush * (0.2 + lumBase * 0.3);
        }
        fx += (gradedBg - baseColor) * gradeStrength;

        // Layer 2: Hex Grid (pixel-space tiling for constant hex size)
        fx += hexGrid(fragCoord, t * 10.0, globalUV, bass, mids, treble, overall);

        // Layer 3: Data Streams (full-screen UV for continuous columns)
        fx += dataStream(globalUV, t * 8.0, bass, overall);

        // Horizontal scan line (from original, was missing)
        vec3 arethaCyan = getArethaCyan();
        float scanFx = scanLine(globalUV, t * 8.0);
        fx += arethaCyan * scanFx;

        // IDS scan lines (treble-driven enhancement)
        if (hasAudio) {
            fx += idsScanLines(globalUV, iTime, treble);
        }

        // Ambient glow
        fx += ambientGlow(globalUV, t * 10.0, overall);

        // Glitch
        fx += glitch(globalUV, iTime, treble);

        // Shimmer (vitality-modulated: subtle when dormant, vivid when highlighted)
        {
            float lum = luminance(baseColor + fx);
            float shimmerAmt = shimmer(globalUV, t * 10.0, lum) * vitalityScale(0.3, 2.0, vitality);
            fx += arethaCyan * shimmerAmt;
        }

        // Vitality: modulate layer intensity (dormant = subdued layers)
        fx *= vitalityScale(0.55, 1.0, vitality);

        // Vignette and compose
        float vig = vignette(globalUV);
        result.rgb = baseColor + fx * vig;
        result.a = bgAlpha;

        // Dormant desaturation
        result.rgb = vitalityDesaturate(result.rgb, vitality);
    }

    // Border (vitality-modulated)
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        vec3 edgeColor = borderColor.rgb;
        if (length(edgeColor) < 0.01) {
            edgeColor = getArethaCyan();
        }

        float bt = iTime * getSpeed() * 5.0;
        float pulse = sin(bt * 2.0) * vitalityScale(0.05, 0.15, vitality) + vitalityScale(0.7, 0.85, vitality);
        edgeColor *= pulse;
        edgeColor = vitalityDesaturate(edgeColor, vitality);

        result.rgb = mix(result.rgb, edgeColor, border * vitalityScale(0.5, 0.8, vitality));
        result.a = max(result.a, border * borderColor.a);
    }

    return result;
}

// Outer glow only — rendered in a separate pass so glow alpha from one zone
// doesn't darken an adjacent zone's fill during blendOver compositing.
vec4 arethaZoneGlow(vec2 fragCoord, vec4 rect, vec4 params, bool isHighlighted) {
    float vitality = zoneVitality(isHighlighted);
    // Corner radius: logical px to device px, clamped to half the zone's smaller side.
    // Shared with the decoration side via zoneSdf() in shared/common.glsl.
    ZoneSDF zoneShape = zoneSdf(fragCoord, rect, params.x);

    float d = zoneShape.d;

    float glowExtent = zoneLen(vitalityScale(10.0, 28.0, vitality));
    if (d > 0.0 && d < glowExtent) {
        float glowSize = zoneLen(vitalityScale(4.0, 8.0, vitality));
        float glowStr = vitalityScale(0.12, 0.4, vitality);
        float glow = expGlow(d, glowSize, glowStr);
        vec3 glowColor = vitalityDesaturate(getArethaCyan(), vitality);
        return vec4(glowColor * glow, glow * vitalityScale(0.25, 0.6, vitality));
    }
    return vec4(0.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Custom Label Composite
// ═══════════════════════════════════════════════════════════════════════════════

vec4 compositeArethaLabels(vec4 color, vec2 fragCoord,
                           float bass, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    vec3 arethaCyan = getArethaCyan();
    vec3 arethaPink = getArethaPink();
    vec3 arethaTeal = getArethaTeal();

    float labelGlowSpread = p_glitchSpread >= 0.0 ? p_glitchSpread : 2.5;
    float labelBrightness = p_scanlineBright >= 0.0 ? p_scanlineBright : 2.0;
    float labelAudioReact = p_surgeReact >= 0.0 ? p_surgeReact : 1.0;
    float glitchFreq = p_glitchFrequency >= 0.0 ? p_glitchFrequency : 0.5;

    // Gaussian halo for smooth glitch outline
    float halo = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.3);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * px * labelGlowSpread).a * w;
        }
    }
    halo /= 16.5;
    float outline = halo * (1.0 - labels.a);

    // Glitch outline: scanline-modulated, RGB-split periodically
    if (outline > 0.01) {
        // Scanlines across outline
        float scanline = step(0.5, fract(fragCoord.y * 0.5));
        vec3 outlineCol = mix(arethaCyan, arethaTeal, scanline);

        // Periodic RGB glitch (~every 2s for 0.15s)
        float glitchCycle = fract(iTime * glitchFreq);
        float glitchOn = step(0.92, glitchCycle);
        if (glitchOn > 0.5) {
            float rShift = texture(uZoneLabels, uv + vec2(px.x * 4.0, 0.0)).a;
            float bShift = texture(uZoneLabels, uv - vec2(px.x * 4.0, 0.0)).a;
            outlineCol = vec3(rShift, halo, bShift) * (1.0 - labels.a);
            outlineCol *= mix(arethaPink, arethaCyan, fract(fragCoord.y * 0.3));
        }

        color.rgb += outlineCol * outline * 0.6;
        color.a = max(color.a, outline * 0.5);
    }

    // Core: data matrix with scanline modulation
    if (labels.a > 0.01) {
        float scan = 0.85 + 0.15 * step(0.5, fract(fragCoord.y * 0.5));
        vec3 core = color.rgb * labelBrightness * scan;
        core += arethaCyan * 0.15 * scan;

        // Bass data surge: flash bright on bass hit
        if (hasAudio && bass > 0.2) {
            float surge = smoothstep(0.2, 0.5, bass);
            core += arethaPink * surge * 0.4 * labelAudioReact;
        }

        color.rgb = mix(color.rgb, core, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════════════

vec4 pImage(vec2 fragCoord) {
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        return vec4(0.0);
    }

    // Audio analysis (computed once for all zones)
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    float overall = getOverallSoft();

    // Pass 1: Zone fills + borders (no outer glow).
    // Glows are separated so their alpha doesn't darken adjacent zone fills
    // during blendOver compositing (the division by outA reduces fill brightness
    // wherever a prior zone's glow alpha has accumulated).
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderArethaZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    // Pass 2: Outer glows (additive, after all fills are composited).
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 glow = arethaZoneGlow(fragCoord, rect, zoneParams[i], isHighlighted);
        if (glow.a > 0.0) {
            color.rgb += glow.rgb;
            color.a = max(color.a, glow.a);
        }
    }

    if (p_showLabels > 0.5)
        color = compositeArethaLabels(color, fragCoord, bass, hasAudio);
    return color;
}
