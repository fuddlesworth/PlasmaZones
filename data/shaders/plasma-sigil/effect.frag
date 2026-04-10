// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Plasma Sigil — PlasmaZones icon as animated energy sigil.
// Five SVG icon rects rendered via SDF with gradient energy flow, racing
// perimeter pulses, energy bridge connections, and audio reactivity.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

// ─── Parameters ─────────────────────────────────────────────────────────────

float getIconScale()     { return customParams[0].x >= 0.0 ? customParams[0].x : 0.7; }
float getStrokeWidth()   { return customParams[0].y >= 0.0 ? customParams[0].y : 0.012; }
float getGlowIntensity() { return customParams[0].z >= 0.0 ? customParams[0].z : 0.8; }
float getPulseSpeed()    { return customParams[0].w >= 0.0 ? customParams[0].w : 1.0; }

float getGradientSpeed() { return customParams[1].x >= 0.0 ? customParams[1].x : 0.3; }
float getSparkRate()     { return customParams[1].y >= 0.0 ? customParams[1].y : 0.6; }
float getAmbientGlow()   { return customParams[1].z >= 0.0 ? customParams[1].z : 0.15; }
float getAudioReact()    { return customParams[1].w >= 0.0 ? customParams[1].w : 1.0; }

float getFillOpacity()   { return customParams[2].x >= 0.0 ? customParams[2].x : 0.85; }
float getBloomStr()      { return customParams[2].y >= 0.0 ? customParams[2].y : 0.4; }

float getLabelGlowSpread() { return customParams[2].z >= 0.0 ? customParams[2].z : 3.0; }
float getLabelBrightness() { return customParams[2].w >= 0.0 ? customParams[2].w : 2.0; }

float getBackgroundStr() { return customParams[3].y >= 0.0 ? customParams[3].y : 0.12; }
float getVeinStr()       { return customParams[3].z >= 0.0 ? customParams[3].z : 0.07; }
float getZoneTint()      { return customParams[3].w >= 0.0 ? customParams[3].w : 0.15; }

// ─── Icon geometry ──────────────────────────────────────────────────────────
// SVG icon rects: centered at origin, normalized by /480. Y-down matches vFragCoord.

const int ICON_COUNT = 5;
const vec4 ICON_RECTS[5] = vec4[5](
    vec4(-0.304,  0.000, 0.196, 0.467),  // tall left column
    vec4( 0.221, -0.204, 0.279, 0.263),  // large top-right
    vec4( 0.117,  0.288, 0.175, 0.179),  // medium bottom-center
    vec4( 0.421,  0.190, 0.079, 0.081),  // small upper-right
    vec4( 0.421,  0.385, 0.079, 0.081)   // small lower-right
);
const float ICON_RADII[5] = float[5](0.058, 0.058, 0.058, 0.046, 0.046);

// Layout divider energy bridges (line segments through the gaps between rects).
// Each vec4 = (start.x, start.y, end.x, end.y)
const int CONN_COUNT = 4;
const vec4 CONN_LINES[4] = vec4[4](
    vec4(-0.083, -0.467, -0.083,  0.467),  // vertical divider: Rect 0 | Rects 1,2
    vec4(-0.058,  0.083,  0.500,  0.083),  // horizontal divider: Rect 1 — Rects 2,3
    vec4( 0.317,  0.108,  0.317,  0.467),  // vertical divider: Rect 2 | Rects 3,4
    vec4( 0.342,  0.288,  0.500,  0.288)   // horizontal divider: Rect 3 — Rect 4
);

// ─── Icon SDF helpers ───────────────────────────────────────────────────────

// Min distance to any icon rect stroke (hollow outline)
float iconStrokeDist(vec2 p, float strokeW) {
    float minD = 1e6;
    for (int i = 0; i < ICON_COUNT; i++) {
        vec2 c  = ICON_RECTS[i].xy;
        vec2 hs = ICON_RECTS[i].zw;
        float d = abs(sdRoundedBox(p - c, hs, ICON_RADII[i])) - strokeW;
        minD = min(minD, d);
    }
    return minD;
}

// Min distance to any icon rect interior (negative = inside a rect)
float iconInteriorDist(vec2 p) {
    float minD = 1e6;
    for (int i = 0; i < ICON_COUNT; i++) {
        vec2 c  = ICON_RECTS[i].xy;
        vec2 hs = ICON_RECTS[i].zw;
        float d = sdRoundedBox(p - c, hs, ICON_RADII[i]);
        minD = min(minD, d);
    }
    return minD;
}

// Line segment distance + parameter (t = position along segment 0-1)
float lineSegDist(vec2 p, vec2 a, vec2 b, out float tParam) {
    vec2 ab = b - a;
    tParam = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
    return length(p - (a + ab * tParam));
}

// ─── Gradient palette (SVG icon colors) ─────────────────────────────────────
// Maps a 0-1 phase through: cyan -> blue -> purple -> rose -> (wrap)

vec3 iconGradient(float t, vec3 cyan, vec3 blue, vec3 purple, vec3 rose) {
    t = fract(t);
    if (t < 0.25) return mix(cyan,   blue,   t * 4.0);
    if (t < 0.50) return mix(blue,   purple, (t - 0.25) * 4.0);
    if (t < 0.75) return mix(purple, rose,   (t - 0.50) * 4.0);
    return mix(rose, cyan, (t - 0.75) * 4.0);
}

// ─── Zone rendering ─────────────────────────────────────────────────────────

vec4 renderSigilZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                     vec4 params, bool isHighlighted,
                     vec3 cyanCol, vec3 blueCol, vec3 purpleCol, vec3 roseCol,
                     float bass, float mids, float treble, bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth  = max(params.y, 2.0);

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    if (d > 30.0) return vec4(0.0);

    float iconScale = getIconScale();
    float strokeW   = getStrokeWidth();
    float glowInt   = getGlowIntensity();
    float pulseSpd  = getPulseSpeed();
    float gradSpd   = getGradientSpeed();
    float sparkRate = getSparkRate();
    float ambientGl = getAmbientGlow();
    float audioR    = getAudioReact();
    float fillOp    = getFillOpacity();
    float bloomStr  = getBloomStr();
    float bgStr     = getBackgroundStr();
    float veinStr   = getVeinStr();
    float zoneTint  = getZoneTint();

    float vitality = zoneVitality(isHighlighted);
    float angle    = atan(p.y, p.x);
    float t        = iTime;
    float midsShift = hasAudio ? t * (gradSpd + mids * 0.3) : t * gradSpd;

    vec3 borderClr = colorWithFallback(borderColor.rgb, vec3(0.4, 0.3, 0.8));

    // ── Map fragment to icon space ────────────────────────────────────────
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    vec2 iconP = localUV - 0.5;
    float zoneAspect = rectSize.x / max(rectSize.y, 1.0);
    iconP.x *= zoneAspect;

    // Bass makes the icon pulse slightly larger
    float audioScale = 1.0 + (hasAudio ? bass * audioR * 0.03 : 0.0);
    iconP /= iconScale * audioScale;

    float strokeDist   = iconStrokeDist(iconP, strokeW);
    float interiorDist = iconInteriorDist(iconP);

    vec4 result = vec4(0.0);

    // ── Inside zone ──────────────────────────────────────────────────────
    if (d < 0.0) {
        vec3 col = vec3(0.0);

        // ── Background: dense layered energy field ─────────────────────────
        vec2 screenUV = fragCoord / max(iResolution, vec2(1.0));
        float screenAspect = iResolution.x / max(iResolution.y, 1.0);
        float vitalBg = vitalityScale(0.35, 1.0, vitality);

        // Layer 1: deep FBM nebula clouds — two counter-rotating octaves
        vec2 nebulaP = (screenUV - 0.5) * 2.5;
        nebulaP.x *= screenAspect;
        float cs1 = cos(0.4), sn1 = sin(0.4);
        mat2 rot1 = mat2(cs1, -sn1, sn1, cs1);
        float n1 = noise2D(nebulaP * 1.0 + t * 0.05);
        float n2 = noise2D(rot1 * nebulaP * 2.2 + t * 0.08 + 50.0);
        float n3 = noise2D(rot1 * rot1 * nebulaP * 4.5 + t * 0.13 + 100.0);
        float n4 = noise2D(nebulaP * 8.0 - t * 0.1 + 200.0);
        float nebula = n1 * 0.4 + n2 * 0.3 + n3 * 0.2 + n4 * 0.1;

        // Contrast-boost the nebula for more visible structure
        nebula = smoothstep(0.2, 0.8, nebula);

        float nebulaPhase = fract(nebula * 1.2 + midsShift * 0.06);
        vec3 nebulaCol = iconGradient(nebulaPhase, cyanCol, blueCol, purpleCol, roseCol);
        float nebulaStr = bgStr * vitalBg;
        nebulaStr *= 1.0 + (hasAudio ? bass * audioR * 0.2 : 0.0);
        col += nebulaCol * nebulaStr * (0.3 + nebula * 0.7);

        // Layer 2: secondary warped nebula at different scale for depth
        vec2 neb2P = nebulaP * 1.6 + vec2(t * 0.03, -t * 0.02);
        float warp = noise2D(nebulaP * 3.0 + t * 0.06) * 0.4;
        float neb2 = noise2D(neb2P + vec2(warp, -warp));
        neb2 = smoothstep(0.3, 0.7, neb2);
        float neb2Phase = fract(neb2 * 0.9 + midsShift * 0.08 + 0.5);
        vec3 neb2Col = iconGradient(neb2Phase, cyanCol, blueCol, purpleCol, roseCol);
        col += neb2Col * neb2 * 0.08 * vitalBg;

        // Layer 3: radial vignette glow from icon center
        float radial = length(iconP) * iconScale;
        float vignette = exp(-radial * radial * 1.2) * 0.15;
        float vigPhase = fract(radial * 0.3 + midsShift * 0.08);
        col += iconGradient(vigPhase, cyanCol, blueCol, purpleCol, roseCol) * vignette * vitalBg;

        // Layer 4: ambient glow cast by the icon strokes (wider falloff)
        float iconProx = exp(-max(strokeDist, 0.0) * 3.5);
        float bgGlow = iconProx * ambientGl * 2.0 * vitalityScale(0.5, 1.5, vitality);
        float ambPhase = fract(atan(iconP.y, iconP.x) / TAU + midsShift * 0.1);
        vec3 ambCol = iconGradient(ambPhase, cyanCol, blueCol, purpleCol, roseCol);
        col += ambCol * bgGlow;

        // Layer 5: drifting particle dust (4 density layers)
        float particles = 0.0;
        for (int pi = 0; pi < 4; pi++) {
            float scale = 6.0 + float(pi) * 5.0;
            vec2 pUV = screenUV * scale;
            pUV += vec2(t * (0.025 + float(pi) * 0.012),
                        t * (0.018 - float(pi) * 0.009));
            vec2 cell = floor(pUV);
            vec2 f = fract(pUV) - 0.5;
            float h = hash21(cell + float(pi) * 73.0);
            vec2 offset = (hash22(cell + float(pi) * 37.0) - 0.5) * 0.35;
            float dist = length(f - offset);
            // Larger particle radius for more visibility
            float bright = smoothstep(0.12, 0.01, dist) * h;
            // Gentle twinkle — slow and biased toward staying visible
            bright *= 0.7 + 0.3 * sin(t * (0.8 + h * 1.5) + h * TAU);
            particles += bright;
        }
        float particlePhase = fract(screenUV.x * 0.5 + screenUV.y * 0.3 + midsShift * 0.1);
        vec3 particleCol = iconGradient(particlePhase, cyanCol, blueCol, purpleCol, roseCol);
        col += particleCol * particles * 0.12 * vitalBg;

        // Layer 6: slow-moving energy veins (sinusoidal interference pattern)
        // Mids widen the vein detection threshold (thicker, more visible veins)
        float midsVeinBoost = hasAudio ? mids * audioR * 0.08 : 0.0;
        float veinThresh = 0.85 - midsVeinBoost;
        vec2 veinP = nebulaP * 4.0;
        float vein1 = sin(veinP.x * 3.0 + veinP.y * 2.0 + t * 0.4 + nebula * 4.0);
        float vein2 = sin(veinP.x * 2.0 - veinP.y * 3.5 + t * 0.3 + n2 * 3.0);
        float vein3 = sin((veinP.x + veinP.y) * 2.5 - t * 0.5);
        float veins = smoothstep(veinThresh, 1.0, vein1) + smoothstep(veinThresh, 1.0, vein2)
                    + smoothstep(veinThresh + 0.03, 1.0, vein3);
        veins = min(veins, 1.0);
        float veinPhase = fract(nebula + midsShift * 0.12);
        vec3 veinCol = iconGradient(veinPhase, cyanCol, blueCol, purpleCol, roseCol);
        col += veinCol * veins * veinStr * vitalBg;

        // Layer 7: fine noise grain
        float grain = noise2D(fragCoord * 0.02 + t * 0.1);
        col += vec3(grain * 0.025) * vitalityScale(0.3, 0.6, vitality);

        // Diagonal gradient phase (used by stroke, racing pulses, and inner glow)
        float gradT = fract((iconP.x + iconP.y) * 0.4 + 0.5 + midsShift * 0.15);

        // ── Icon stroke rendering ─────────────────────────────────────────
        if (strokeDist < 0.06) {
            float strokeMask = 1.0 - smoothstep(-0.002, 0.015, strokeDist);
            float bloomMask  = 1.0 - smoothstep(0.0, 0.06, strokeDist);

            vec3 strokeCol = iconGradient(gradT, cyanCol, blueCol, purpleCol, roseCol);

            // Slow breathing — high baseline, gentle variation
            float pulse = 0.9 + 0.1 * sin(t * pulseSpd * 0.8);
            pulse *= vitalityScale(0.5, 1.0, vitality);

            // Audio: subtle brightening, not a flash
            float resonance = 1.0 + (hasAudio ? bass * audioR * 0.3 : 0.0);

            col += strokeCol * strokeMask * pulse * resonance * glowInt;
            col += strokeCol * bloomMask * bloomStr * pulse * resonance * 0.35;

            // ── Racing perimeter pulses ───────────────────────────────────
            for (int i = 0; i < ICON_COUNT; i++) {
                vec2 c  = ICON_RECTS[i].xy;
                vec2 hs = ICON_RECTS[i].zw;
                float r = ICON_RADII[i];
                float sd = abs(sdRoundedBox(iconP - c, hs, r)) - strokeW;

                if (sd < 0.02) {
                    float edgeMask = 1.0 - smoothstep(-0.002, 0.015, sd);
                    float ang = atan(iconP.y - c.y, iconP.x - c.x);

                    // Two racing pulses per rect, opposite directions
                    float racePos1 = fract(t * pulseSpd * 0.4 + float(i) * 0.2);
                    float raceAngle1 = racePos1 * TAU;
                    float angDist1 = 1.0 - abs(mod(ang - raceAngle1 + PI, TAU) - PI) / PI;
                    float race1 = pow(max(angDist1, 0.0), 12.0);

                    float racePos2 = fract(-t * pulseSpd * 0.3 + float(i) * 0.4 + 0.5);
                    float raceAngle2 = racePos2 * TAU;
                    float angDist2 = 1.0 - abs(mod(ang - raceAngle2 + PI, TAU) - PI) / PI;
                    float race2 = pow(max(angDist2, 0.0), 12.0);

                    float raceEnergy = (race1 + race2 * 0.5) * edgeMask;
                    raceEnergy *= vitalityScale(0.15, 0.7, vitality);

                    float racePhase = fract(gradT + float(i) * 0.2 + racePos1 * 0.3);
                    vec3 raceCol = iconGradient(racePhase, cyanCol, blueCol, purpleCol, roseCol);
                    col += raceCol * raceEnergy * glowInt * 0.5;
                }
            }
        }

        // ── Inner glow within icon rects ──────────────────────────────────
        if (interiorDist < 0.0) {
            float innerDist = -interiorDist;
            float bevelGlow = exp(-innerDist * 12.0) * 0.25;

            // Per-rect color phase
            float rectPhase = 0.0;
            for (int i = 0; i < ICON_COUNT; i++) {
                vec2 c  = ICON_RECTS[i].xy;
                vec2 hs = ICON_RECTS[i].zw;
                if (sdRoundedBox(iconP - c, hs, ICON_RADII[i]) < 0.0) {
                    rectPhase = float(i) / float(ICON_COUNT);
                    break;
                }
            }

            float phase = fract(rectPhase + midsShift * 0.2);
            vec3 innerCol = iconGradient(phase, cyanCol, blueCol, purpleCol, roseCol);

            // Slow phase drift per rect — no abrupt pulsing
            float innerPhase = 0.8 + 0.2 * sin(t * pulseSpd * 0.5 + rectPhase * TAU);
            col += innerCol * bevelGlow * innerPhase * vitalityScale(0.3, 1.0, vitality);

            // Subtle interior fill
            float fillStr = 0.025 * vitalityScale(0.3, 1.0, vitality);
            fillStr *= 1.0 + (hasAudio ? bass * audioR * 0.2 : 0.0);
            col += innerCol * fillStr;
        }

        // ── Energy bridge connections (layout dividers) ───────────────────
        float connGlowTotal = 0.0;
        vec3 connColTotal = vec3(0.0);
        for (int i = 0; i < CONN_COUNT; i++) {
            vec2 a = CONN_LINES[i].xy;
            vec2 b = CONN_LINES[i].zw;
            float tParam;
            float ld = lineSegDist(iconP, a, b, tParam);

            if (ld < 0.06) {
                float lineMask = exp(-ld * 80.0);

                // Gentle traveling energy packets along the divider
                float packet = sin(tParam * 12.0 - t * pulseSpd * 2.0 + float(i) * 1.5);
                packet = max(packet, 0.0);

                // Steady trace with gentle bright packets
                float energy = lineMask * (0.3 + packet * 0.7);
                energy *= vitalityScale(0.15, 0.5, vitality);

                float connPhase = fract(tParam * 0.3 + midsShift * 0.2 + float(i) * 0.25);
                vec3 connCol = iconGradient(connPhase, cyanCol, blueCol, purpleCol, roseCol);

                connColTotal += connCol * energy;
                connGlowTotal += energy;
            }
        }
        col += connColTotal * glowInt * 0.5;

        // ── Treble: edge sparks ───────────────────────────────────────────
        if (hasAudio && treble > 0.05 && strokeDist > -0.02 && strokeDist < 0.08) {
            float sparkProx = 1.0 - smoothstep(0.0, 0.08, abs(strokeDist));
            float sparkSeed = noise2D(iconP * 30.0 + t * 1.5);
            float sparkThresh = 1.0 - treble * 0.35;
            float spark = smoothstep(sparkThresh, sparkThresh + 0.05, sparkSeed);
            spark *= sparkProx * treble * audioR * sparkRate;

            vec3 sparkCol = mix(cyanCol, vec3(1.0), 0.3);
            col += sparkCol * spark * 0.6;
        }

        // ── Zone fill color tint ──────────────────────────────────────────
        col = mix(col, col * fillColor.rgb, zoneTint);

        // Vitality
        col = vitalityDesaturate(col, vitality);
        col *= vitalityScale(0.45, 1.0, vitality);

        result.rgb = clamp(col, 0.0, 1.0);

        // Alpha: base fill + stronger where icon is visible
        float iconPresence = smoothstep(0.03, -0.01, strokeDist) * 0.2
                           + connGlowTotal * 0.1;
        result.a = fillOp * vitalityScale(0.7, 1.0, vitality);
        result.a = max(result.a, iconPresence);

        if (isHighlighted) {
            result.a = min(result.a + 0.04, 1.0);
        }
    }

    // ── Border: gradient flow with per-zone border color tint ───────────
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float flowSpeed = vitalityScale(0.3, 2.0, vitality);
        float flowRange = vitalityScale(0.1, 0.4, vitality);
        float flow = angularNoise(angle, 8.0, -t * flowSpeed) * flowRange
                   + (1.0 - flowRange * 0.5);

        float borderPhase = fract(angle / TAU + midsShift * 0.1);
        vec3 borderGrad = iconGradient(borderPhase, cyanCol, blueCol, purpleCol, roseCol);

        // Blend per-zone border color with the gradient (borderColor influences tint)
        vec3 borderCol = mix(borderGrad, borderClr * luminance(borderGrad) + borderGrad * 0.3, 0.3);
        borderCol *= flow;

        // Mids: color cycling between border color and gradient complement
        if (hasAudio && mids > 0.05) {
            float cyclePhase = fract(t * (0.2 + mids * 0.12));
            vec3 complement = iconGradient(borderPhase + 0.5, cyanCol, blueCol, purpleCol, roseCol);
            borderCol = mix(borderCol, complement * flow, smoothstep(0.05, 0.4, mids) * 0.25);
        }

        // Subtle bass border brightening
        float borderEnergy = 1.0 + (hasAudio ? bass * audioR * 0.2 : 0.0);
        borderCol *= borderEnergy;

        // Gentle racing border accent
        float pulsePos = hasAudio ? fract(t * 0.8) * TAU : fract(t * 0.4) * TAU;
        float angDist = 1.0 - abs(mod(angle - pulsePos + PI, TAU) - PI) / PI;
        borderCol *= 1.0 + angDist * vitalityScale(0.05, 0.2, vitality);

        if (isHighlighted) {
            float breathe = 0.92 + 0.08 * sin(t * 1.5);
            borderCol *= breathe;
        }

        float borderAlpha = border * vitalityScale(0.8, 0.95, vitality);
        result.rgb = mix(result.rgb, borderCol, borderAlpha);
        result.a = max(result.a, border * 0.96);
    }

    // ── Outer glow: chromatic gradient ───────────────────────────────────
    if (d > 0.0 && d < 24.0) {
        float glowRadius  = vitalityScale(5.0, 10.0, vitality);
        float glowFalloff = vitalityScale(0.3, 0.55, vitality);

        // Gentle bass wavefront expansion
        if (hasAudio) {
            float waveCycle = fract(t * 0.7);
            float waveRadius = waveCycle * 16.0;
            float waveBand = exp(-abs(d - waveRadius) * 0.6) * (1.0 - waveCycle);
            glowRadius += waveBand * bass * 3.0;
            glowFalloff += waveBand * bass * 0.15;
        }

        float glow = expGlow(d, glowRadius, glowFalloff);
        glow *= vitalityScale(0.3, 1.0, vitality);

        float glowPhase = fract(angle / TAU + midsShift * 0.05);
        vec3 glowColor = iconGradient(glowPhase, cyanCol, blueCol, purpleCol, roseCol);

        // Chromatic split outer glow
        vec3 outerGlowCol = vec3(
            expGlow(d - 1.0, glowRadius, glowFalloff),
            glow,
            expGlow(d + 1.0, glowRadius, glowFalloff)
        ) * glowColor;

        result.rgb += outerGlowCol;
        result.a = max(result.a, glow * 0.5);
    }

    return result;
}

// ─── Label compositing ──────────────────────────────────────────────────────

vec4 compositeSigilLabels(vec4 color, vec2 fragCoord,
                          vec3 cyanCol, vec3 purpleCol,
                          float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    float glowSpread = getLabelGlowSpread();
    float labelBright = getLabelBrightness();

    // Gaussian halo (5x5 kernel)
    float halo = 0.0;
    float haloW = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.3);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * px * glowSpread).a * w;
            haloW += w;
        }
    }
    halo /= haloW;

    // Gradient-colored glow halo
    if (halo > 0.003) {
        float haloEdge = halo * (1.0 - labels.a);

        vec3 glowClr = mix(cyanCol, purpleCol, sin(iTime * 2.0) * 0.5 + 0.5);
        float haloBright = haloEdge * (0.4 + (hasAudio ? bass * 0.5 : 0.0));
        color.rgb += glowClr * haloBright;

        // Treble sparkles at label edges
        if (hasAudio && treble > 0.1) {
            float sparkNoise = noise2D(uv * 50.0 + iTime * 1.5);
            float spark = smoothstep(0.75, 0.95, sparkNoise) * treble * 0.8;
            color.rgb += cyanCol * haloEdge * spark;
        }

        color.a = max(color.a, haloEdge * 0.5);
    }

    // Core label
    if (labels.a > 0.01) {
        vec3 boosted = color.rgb * labelBright;
        float bassPulse = hasAudio ? 1.0 + bass * 0.4 : 1.0;
        boosted *= bassPulse;
        color.rgb = mix(color.rgb, boosted, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}

// ─── Main ───────────────────────────────────────────────────────────────────

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 cyanCol   = colorWithFallback(customColors[0].rgb, vec3(0.133, 0.827, 0.933));
    vec3 blueCol   = colorWithFallback(customColors[1].rgb, vec3(0.231, 0.510, 0.965));
    vec3 purpleCol = colorWithFallback(customColors[2].rgb, vec3(0.659, 0.333, 0.969));
    vec3 roseCol   = colorWithFallback(customColors[3].rgb, vec3(0.957, 0.247, 0.369));

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass   = getBassSoft();
    float mids   = getMidsSoft();
    float treble = getTrebleSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderSigilZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            cyanCol, blueCol, purpleCol, roseCol, bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (customParams[3].x > 0.5)
        color = compositeSigilLabels(color, fragCoord, cyanCol, purpleCol,
                                     bass, mids, treble, hasAudio);

    fragColor = clampFragColor(color);
}
