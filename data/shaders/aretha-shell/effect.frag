// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Aretha Shell — Ghost in the Shell / Aretha Dark Layered Shader
// Ported from Ghostty shader: ~/.config/ghostty/shaders/aretha_shell.glsl
// Original: MonoBall-inspired three-layer composite (Color Grade + Hex Grid + Data Streams)
// PlasmaZones enhancements: audio reactivity, bidirectional streams, IDS scan lines

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// ═══════════════════════════════════════════════════════════════════════════════
// PARAMETERS  (customParams slots 0-15, customColors slots 0-4)
// ═══════════════════════════════════════════════════════════════════════════════

float getSpeed()           { return customParams[0].x >= 0.0 ? customParams[0].x : 0.08; }
float getGradeIntensity()  { return customParams[0].y >= 0.0 ? customParams[0].y : 0.25; }
float getGradeSaturation() { return customParams[0].z >= 0.0 ? customParams[0].z : 1.08; }
float getShimmerIntensity(){ return customParams[0].w >= 0.0 ? customParams[0].w : 0.02; }
float getHexPixelSize()    { return customParams[1].x >= 0.0 ? customParams[1].x : 35.0; }
float getHexLineThickness(){ return customParams[1].y >= 0.0 ? customParams[1].y : 0.05; }
float getHexOpacity()      { return customParams[1].z >= 0.0 ? customParams[1].z : 0.18; }
float getHexPulseSpeed()   { return customParams[1].w >= 0.0 ? customParams[1].w : 1.5; }
float getHexScanSpeed()    { return customParams[2].x >= 0.0 ? customParams[2].x : 1.0; }
float getStreamColumns()   { return customParams[2].y >= 0.0 ? customParams[2].y : 35.0; }
float getStreamOpacity()   { return customParams[2].z >= 0.0 ? customParams[2].z : 0.15; }
float getStreamSpeed()     { return customParams[2].w >= 0.0 ? customParams[2].w : 0.8; }
float getTrailLength()     { return customParams[3].x >= 0.0 ? customParams[3].x : 0.20; }
float getAudioSensitivity(){ return customParams[3].y >= 0.0 ? customParams[3].y : 1.0; }
float getFillOpacity()     { return customParams[3].z >= 0.0 ? customParams[3].z : 0.95; }
float getDataSurgeIntensity() { return customParams[3].w >= 0.0 ? customParams[3].w : 1.0; }

// ═══════════════════════════════════════════════════════════════════════════════
// COLORS
// ═══════════════════════════════════════════════════════════════════════════════

const vec3 arethaBg       = vec3(0.098, 0.153, 0.259);  // #192742
const vec3 arethaLavender = vec3(0.482, 0.388, 0.776);  // #7b63c6

const vec3 gradeShadow    = vec3(0.098, 0.153, 0.259);
const vec3 gradeMid       = vec3(0.000, 0.620, 0.741);
const vec3 gradeHighlight = vec3(0.333, 0.667, 1.000);

vec3 getBackgroundColor() {
    vec3 bg = customColors[4].rgb;
    return length(bg) > 0.01 ? bg : arethaBg;
}

vec3 getArethaCyan()   { vec3 c = customColors[0].rgb; return length(c) > 0.01 ? c : vec3(0.333, 0.667, 1.000); }
vec3 getArethaPink()   { vec3 c = customColors[1].rgb; return length(c) > 0.01 ? c : vec3(1.000, 0.333, 0.498); }
vec3 getArethaTeal()   { vec3 c = customColors[2].rgb; return length(c) > 0.01 ? c : vec3(0.000, 0.620, 0.741); }
vec3 getArethaPurple() { vec3 c = customColors[3].rgb; return length(c) > 0.01 ? c : vec3(0.333, 0.333, 1.000); }

// ═══════════════════════════════════════════════════════════════════════════════
// UTILITIES
// ═══════════════════════════════════════════════════════════════════════════════


// ═══════════════════════════════════════════════════════════════════════════════
// LAYER 1: NEON COLOR GRADE  (from original, unchanged)
// ═══════════════════════════════════════════════════════════════════════════════

vec3 colorGrade(vec3 color, float t) {
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
    vec2 scaledUV = pixelCoord / HEX_PIXEL_SIZE;
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

    // Audio gently modulates pulse: mids speed it up, bass widens it
    if (hasAudio) {
        pulseSpeed += mids * audioSens * 1.5;
        pulseAmp += bass * audioSens * 0.4;
    }

    float pulse = sin(t * pulseSpeed + hexId.x * 0.5 + hexId.y * 0.3) * 0.5 + 0.5;

    // Scan line sweeping down (original behavior)
    float scanSpeed = HEX_SCAN_SPEED;
    if (hasAudio) {
        scanSpeed += mids * audioSens * 0.5;
    }
    float scan = sin(hexCenter.y * 0.2 - t * scanSpeed) * 0.5 + 0.5;
    scan = pow(scan, 8.0);

    // Grid intensity: original formula
    float gridIntensity = edge * (0.3 + pulse * pulseAmp);
    gridIntensity += scan * 0.25;

    // Audio: bass gently brightens interior of hex cells
    if (hasAudio) {
        float interiorGlow = (1.0 - smoothstep(0.0, 0.45, d)) * bass * audioSens * 0.3;
        gridIntensity += interiorGlow;
        gridIntensity += overall * audioSens * 0.08;
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

    // Audio: bass gently tints toward pink
    if (hasAudio) {
        gridColor = mix(gridColor, arethaPink, bass * audioSens * 0.2);
    }

    return gridColor * gridIntensity * HEX_OPACITY;
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

    // Trail with gentle bass extension
    float effectiveTrail = TRAIL_LENGTH;
    if (hasAudio) {
        effectiveTrail *= 1.0 + bass * audioSens * 0.3;
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

    // Brighter heads (original, with bass boost)
    if (head > 0.5) {
        float headWhiteness = hasAudio ? 0.4 + bass * audioSens * 0.2 : 0.4;
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
    // Audio gently lifts ambient
    if (iAudioSpectrumSize > 0) {
        intensity += overall * getAudioSensitivity() * 0.04;
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
            float separation = (blockRand - 0.7) * 0.02 * intensity;

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

    float borderRadius = max(params.x, 4.0);
    float borderWidth  = max(params.y, 1.0);

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);
    localUV = clamp(localUV, 0.0, 1.0);

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        float t = iTime * SPEED;
        vec3 fx = vec3(0.0);

        // Full-screen UV for continuous effects across zones
        vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));

        // Base color
        vec3 baseColor = getBackgroundColor();
        float bgAlpha = fillColor.a > 0.01 ? fillColor.a : getFillOpacity();

        // Layer 1: Color Grade
        vec3 gradedBg = colorGrade(baseColor, t * 10.0);
        float gradeStrength = 0.5;
        if (hasAudio) {
            gradeStrength += overall * getAudioSensitivity() * 0.15;
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

        // Shimmer on highlighted zones
        if (isHighlighted) {
            float lum = luminance(baseColor + fx);
            fx += arethaCyan * shimmer(globalUV, t * 10.0, lum) * 2.0;
        }

        // Vignette and compose
        float vig = vignette(globalUV);
        result.rgb = baseColor + fx * vig;
        result.a = bgAlpha;

        // Brighten highlighted zones
        if (isHighlighted) {
            result.rgb *= 1.15;
            result.a = min(result.a + 0.1, 1.0);
        }
    }

    // Border
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        vec3 edgeColor = borderColor.rgb;
        if (length(edgeColor) < 0.01) {
            edgeColor = getArethaCyan();
        }

        float bt = iTime * getSpeed() * 5.0;
        float pulse = sin(bt * 2.0) * 0.15 + 0.85;
        edgeColor *= pulse;

        result.rgb = mix(result.rgb, edgeColor, border * 0.8);
        result.a = max(result.a, border * borderColor.a);
    }

    // Outer glow for highlighted zones
    if (isHighlighted && d > 0.0 && d < 20.0) {
        float glow = expGlow(d, 8.0, 0.4);
        result.rgb += getArethaCyan() * glow;
        result.a = max(result.a, glow * 0.6);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════════════

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    // Audio analysis (computed once for all zones)
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBass();
    float mids    = getMids();
    float treble  = getTreble();
    float overall = getOverall();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderArethaZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);
    fragColor = clampFragColor(color);
}
