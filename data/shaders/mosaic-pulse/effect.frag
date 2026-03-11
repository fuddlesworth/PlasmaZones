// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

/*
 * MOSAIC PULSE — Audio-Reactive Stained Glass Mosaic
 *
 * Colorful tiled mosaic grid with random shapes (circles, diamonds, squares),
 * HSL color variation, sparkles, grid lines, and dithered posterization.
 * Bass drives per-tile hue scatter, mids shift hue directionally, treble triggers tile pops and sparkles.
 *
 * Parameters (customParams):
 *   [0].x = gridDensity     — number of vertical tiles
 *   [0].y = edgeSoftness    — shape edge anti-alias width
 *   [0].z = gridLineWeight  — grid line darkness (0=none, 1=full)
 *   [0].w = posterize       — color quantization levels
 *   [1].x = shapeChance     — probability a cell gets a shape (0-1)
 *   [1].y = shapeSize       — base shape radius
 *   [1].z = sparkleChance   — probability a cell gets a sparkle (0-1)
 *   [1].w = speed           — animation speed multiplier
 *   [2].x = reactivity      — audio sensitivity multiplier
 *   [2].y = fillOpacity     — zone fill alpha
 *
 * Colors:
 *   customColors[0] = hue center  (default: steel blue #4488cc)
 *   customColors[1] = shape tint  (default: warm white #fffff2)
 */

// ─── Mosaic noise (prefixed to avoid common.glsl collision) ──────

float mosaicHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float mosaicNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mosaicHash(i), mosaicHash(i + vec2(1.0, 0.0)), u.x),
        mix(mosaicHash(i + vec2(0.0, 1.0)), mosaicHash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

float mosaicFbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * mosaicNoise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

// ─── HSL → RGB ───────────────────────────────────────────────────

vec3 hsl2rgb(vec3 c) {
    vec3 rgb = clamp(
        abs(mod(c.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0,
        0.0, 1.0
    );
    return c.z + c.y * (rgb - 0.5) * (1.0 - abs(2.0 * c.z - 1.0));
}

// ─── Shape SDF: 0=circle, 1=diamond, 2=square, 3=cross ──────────

float shapeSDF(vec2 p, int t) {
    if (t == 0) return length(p);
    if (t == 1) return abs(p.x) + abs(p.y);
    if (t == 2) return max(abs(p.x), abs(p.y));
    return min(abs(p.x), abs(p.y));
}

// ─── Per-zone rendering ─────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted,
                float bass, float mids, float treble, float overall, bool hasAudio)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    // Parameters with defaults
    float gridDensity   = customParams[0].x >= 0.0 ? customParams[0].x : 64.0;
    float edgeSoftness  = customParams[0].y >= 0.0 ? customParams[0].y : 0.18;
    float gridLineW     = customParams[0].z >= 0.0 ? customParams[0].z : 0.75;
    float posterLevels  = customParams[0].w >= 0.0 ? customParams[0].w : 8.0;
    posterLevels = max(posterLevels, 1.0);
    float shapeChance   = customParams[1].x >= 0.0 ? customParams[1].x : 0.62;
    float shapeSize     = customParams[1].y >= 0.0 ? customParams[1].y : 0.26;
    float sparkleChance = customParams[1].z >= 0.0 ? customParams[1].z : 0.04;
    float speed         = customParams[1].w >= 0.0 ? customParams[1].w : 1.25;
    float reactivity    = customParams[2].x >= 0.0 ? customParams[2].x : 1.0;
    float fillOpacity   = customParams[2].y >= 0.0 ? customParams[2].y : 0.9;

    // Colors — fallbacks match the original ShaderToy palette
    // hueCenter: #4099BF ≈ HSL hue 0.55 (cyan-blue, matching the original 0.55 center)
    vec3 hueCenter = colorWithFallback(customColors[0].rgb, vec3(0.251, 0.6, 0.749));
    vec3 shapeTint = colorWithFallback(customColors[1].rgb, vec3(1.0, 1.0, 0.949));

    // ── Highlighted vs dormant ──────────────────────────────
    float vitality = isHighlighted ? 1.0 : 0.3;

    if (isHighlighted) {
        reactivity *= 1.4;
        speed *= 1.2;
    } else {
        hueCenter = mix(hueCenter, vec3(luminance(hueCenter)), 0.5);
        shapeTint = mix(shapeTint, vec3(luminance(shapeTint)), 0.4);
        reactivity *= 0.5;
        speed *= 0.6;
    }

    // Zone geometry
    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    float energy    = hasAudio ? overall * reactivity : 0.0;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(iTime * 0.8 * PI)) * 0.5;

    // Sweep direction for glow (needs to be accessible outside interior block)
    float tGlow = iTime * speed;
    vec2 sweepDirGlow = normalize(vec2(sin(tGlow * 0.4), cos(tGlow * 0.6)));

    vec4 result = vec4(0.0);

    // ── Zone interior ───────────────────────────────────────

    if (d < 0.0) {
        float t = iTime * speed;

        // Derive base hue from hueCenter color (RGB → HSL hue via atan2)
        float hueX = 1.732 * (hueCenter.g - hueCenter.b);
        float hueY = 2.0 * hueCenter.r - hueCenter.g - hueCenter.b;
        float hueCenterVal = (abs(hueX) + abs(hueY) < 0.01)
            ? 0.0
            : fract(atan(hueX, hueY) / TAU);

        // Virtual grid coordinates — screen-space for continuity across zones
        float vh = gridDensity;
        float vw = vh * iResolution.x / max(iResolution.y, 1.0);
        vec2 vUV = fragCoord / max(iResolution.xy, vec2(1.0)) * vec2(vw, vh);

        vec2 cellId = floor(vUV);
        vec2 cellP = fract(vUV) - 0.5;
        vec2 cellNorm = cellId / vec2(vw, vh);

        // Audio modulation — tile-grid-aware (no uniform scaling)
        // Bass hue scatter: tiles spread further from center hue on bass hits
        float bassScatter = hasAudio ? smoothstep(0.1, 0.4, bass) * reactivity * 0.15 : 0.0;

        // Directional color sweep: mids drive a hue gradient across the grid
        vec2 sweepDir = normalize(vec2(sin(t * 0.4), cos(t * 0.6)));
        float sweepPhase = hasAudio
            ? fract(dot(cellId, sweepDir) * 0.05 - iTime * mids * 0.5) * mids * reactivity * 0.3
            : 0.0;

        // Tile pop: treble causes individual tiles to briefly flash
        float popFreq = customParams[3].y >= 0.0 ? customParams[3].y : 4.0;
        float popHash = mosaicHash(cellId + floor(t * popFreq) * 0.1);
        float popActive = smoothstep(0.5, 0.8, treble)
                        * step(popHash, treble * 0.6)
                        * (1.0 - fract(t * 3.0 + popHash * TAU));
        float audioPop = hasAudio ? max(popActive, 0.0) * reactivity : 0.0;

        // Ripple rings: overall energy creates concentric rings from grid center
        float rippleDist = length(cellId - vec2(vw, vh) * 0.5);
        float ripplePhase = fract(rippleDist * 0.1 - iTime * 0.8);
        float ripple = smoothstep(0.0, 0.15, ripplePhase) * smoothstep(0.3, 0.15, ripplePhase);
        float audioRipple = hasAudio ? ripple * overall * reactivity : 0.0;

        // ── Tile background color (HSL) ──────────────────────
        float n = mosaicFbm(cellId * 0.35 + t * 0.15);

        float hue = hueCenterVal
            + 0.25 * n
            + 0.05 * sin(cellNorm.x * 4.0 + t)
            + 0.05 * cos(cellNorm.y * 3.0 - t)
            + sweepPhase;  // directional sweep replaces uniform mids hue shift
        hue += (mosaicHash(cellId + 123.4) - 0.5) * (0.3 + bassScatter);
        hue = fract(hue);

        float sat = 0.4 + 0.15 * mosaicFbm(cellId * 0.6 - t * 0.2);
        sat += (mosaicHash(cellId + 234.5) - 0.5) * 0.25;
        sat = clamp(sat, 0.2, 0.8);

        float lit = 0.55 + 0.25 * mosaicFbm(cellId * 0.2 + t)
            + audioPop * 0.35          // tile pop: treble makes individual tiles flash bright
            + audioRipple * 0.12;      // ripple rings: concentric energy rings modulate brightness
        lit += (mosaicHash(cellId + 345.6) - 0.5) * 0.25;
        lit = clamp(lit, 0.35, 0.9);

        vec3 bg = hsl2rgb(vec3(hue, sat, lit));
        vec3 col = bg;

        // ── Shape overlay ────────────────────────────────────
        float rnd = mosaicHash(cellId);
        float shapeThreshold = 1.0 - shapeChance;

        if (rnd > shapeThreshold) {
            int stype = int(floor(rnd * 4.0));

            // Base pulse + pop-driven shape expansion
            float pulse = 0.04 * sin(t * 3.0 + rnd * 10.0)
                + 0.03 * mosaicNoise(cellId + t)
                + audioPop * 0.06;        // popping tiles briefly expand their shape

            float r = shapeSize + pulse;

            float sd = shapeSDF(cellP, stype);
            float mask = 1.0 - smoothstep(r, r + edgeSoftness, sd);

            // Shape palette — original pastels, subtly tinted by shapeTint
            vec3 pal[4];
            pal[0] = vec3(1.0, 0.95, 0.85) * shapeTint;  // warm white
            pal[1] = vec3(0.85, 0.92, 1.0) * shapeTint;  // cool white
            pal[2] = vec3(1.0, 0.82, 0.9) * shapeTint;   // pink white
            pal[3] = vec3(0.9, 1.0, 0.85) * shapeTint;   // green white

            int cid = int(floor(mosaicHash(cellId + 9.7) * 4.0));
            vec3 shapeCol = pal[cid];

            // Audio-reactive shape color: pop whitens
            if (hasAudio) {
                shapeCol = mix(shapeCol, vec3(1.0), audioPop * 0.5);
            }

            col = mix(col, shapeCol, mask);
        }

        // ── Sparkles (tile-pop driven) ─────────────────────────
        float spark = mosaicHash(cellId + 17.3);
        float sparkThreshold = 1.0 - sparkleChance;
        // Tile pop lowers threshold — popping tiles are more likely to sparkle
        float effectiveSparkThresh = sparkThreshold - audioPop * 0.3;

        if (spark > effectiveSparkThresh) {
            float sd = length(cellP - vec2(0.3, -0.3));
            float sparkBright = 1.0 - smoothstep(0.02, 0.2, sd);
            // Sparkle intensity tracks pop + ripple (spatial, not uniform)
            float sparkIntensity = 0.7 + audioPop * 0.6 + audioRipple * 0.3;
            vec3 sparkleCol = colorWithFallback(customColors[3].rgb, vec3(1.0, 0.95, 0.8));
            col += sparkleCol * sparkBright * sparkIntensity;
        }

        // ── Grid lines ───────────────────────────────────────
        if (gridLineW > 0.01) {
            vec2 g = fract(vUV);
            float line = min(min(g.x, 1.0 - g.x), min(g.y, 1.0 - g.y));
            float lineDarken = mix(1.0 - gridLineW * 0.25, 1.0, smoothstep(0.0, 0.06, line));
            col *= lineDarken;
        }

        // ── Dithered posterization ───────────────────────────
        float ditherAmt = 0.04;
        float dither = mosaicHash(fragCoord + iTime) * ditherAmt;
        col += dither;
        col = clamp(col, 0.0, 1.0);  // clamp before posterization
        col = floor(col * posterLevels) / posterLevels;

        // Dormant desaturation
        if (!isHighlighted) {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.4);
            col *= 0.7;
        }

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);

        // Inner edge glow — pulses with ripple rings near zone edges
        float edgeRipple = hasAudio ? audioRipple * 0.8 : 0.0;
        float innerGlow = exp(d / mix(25.0, 12.0, vitality)) * mix(0.04, 0.15, vitality) * (1.0 + edgeRipple);
        result.rgb += hueCenter * innerGlow;
    }

    // ── Border ──────────────────────────────────────────────

    float coreWidth = borderWidth * mix(0.5, 0.9, vitality);
    float core = softBorder(d, coreWidth);
    if (core > 0.0) {
        float angle = atan(p.x, -p.y) / TAU + 0.5;

        float borderEnergy = 1.0 + energy * mix(0.2, 1.0, vitality) + idlePulse * 0.3;
        vec3 coreColor = hueCenter * mix(1.0, 2.0, vitality) * borderEnergy;

        float flowSpeed = mix(0.3, 2.0, vitality);
        float flowRange = mix(0.1, 0.4, vitality);
        float flow = angularNoise(angle, 10.0, -iTime * flowSpeed) * flowRange + (1.0 - flowRange * 0.5);
        coreColor *= flow;

        if (isHighlighted) {
            float breathe = 0.8 + 0.2 * sin(iTime * 3.0 + energy * 4.0);
            coreColor *= breathe;
            float accentTrace = angularNoise(angle, 6.0, iTime * 2.5);
            coreColor = mix(coreColor, shapeTint * borderEnergy, accentTrace * 0.3);
        }

        coreColor = mix(coreColor, vec3(1.0), core * mix(0.25, 0.6, vitality));

        if (hasAudio && bass > 0.5) {
            float flash = (bass - 0.5) * 2.0 * vitality;
            coreColor = mix(coreColor, vec3(1.0, 0.6, 0.2) * 2.0, flash * core * 0.3);
        }

        result.rgb = max(result.rgb, coreColor * core);
        result.a = max(result.a, core);
    }

    // ── Outer glow ──────────────────────────────────────────

    float baseGlowR = mix(8.0, 20.0, vitality);
    float bassGlowR = mix(3.0, 6.0, vitality);
    // Glow radius pulses with bass beat but uses a smoothed envelope, not raw value
    float bassEnvelope = hasAudio ? smoothstep(0.2, 0.7, bass) * bass * reactivity : idlePulse;
    float glowRadius = baseGlowR + bassGlowR * bassEnvelope + 5.0 * energy;
    if (d > 0.0 && d < glowRadius) {
        float glowStr = mix(0.12, 0.35, vitality);
        float glow1 = expGlow(d, glowRadius * 0.2, glowStr);
        float glow2 = expGlow(d, glowRadius * 0.5, glowStr * 0.35);

        vec3 glowColor = hueCenter;
        if (isHighlighted) {
            float glowAngle = atan(p.x, -p.y) / TAU + 0.5;
            glowColor = mix(hueCenter, shapeTint, angularNoise(glowAngle, 4.0, iTime * 0.8) * 0.5);
        }
        // Directional sweep tints the glow as it passes the zone edge
        if (hasAudio && mids > 0.2) {
            float zoneSweep = fract(dot(p / max(length(rectSize), 1.0), sweepDirGlow) - iTime * mids * 0.3);
            float sweepMask = smoothstep(0.0, 0.3, zoneSweep) * smoothstep(0.6, 0.3, zoneSweep);
            glowColor = mix(glowColor, shapeTint * 1.5, sweepMask * mids * vitality);
        }

        result.rgb += glowColor * (glow1 + glow2);
        result.a = max(result.a, (glow1 + glow2) * 0.5);
    }

    return result;
}

// ─── Custom Label Composite ─────────────────────────────────────

vec4 compositeMosaicLabels(vec4 color, vec2 fragCoord,
                            float bass, float mids, float treble, float overall, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    // Colors — match renderZone palette
    vec3 hueCenter = colorWithFallback(customColors[0].rgb, vec3(0.251, 0.6, 0.749));
    vec3 shapeTint = colorWithFallback(customColors[1].rgb, vec3(1.0, 1.0, 0.949));
    float posterLevels = customParams[0].w >= 0.0 ? customParams[0].w : 8.0;
    posterLevels = max(posterLevels, 1.0);
    float reactivity = customParams[2].x >= 0.0 ? customParams[2].x : 1.0;

    float labelGlowSpread = customParams[2].z >= 0.0 ? customParams[2].z : 2.0;
    float labelBright = customParams[2].w >= 0.0 ? customParams[2].w : 0.7;
    float labelAudioMul = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;

    // Gaussian halo for smooth beveled "lead" border
    float halo = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.3);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * px * labelGlowSpread).a * w;
        }
    }
    halo /= 16.5;
    float leadBorder = halo * (1.0 - labels.a);

    // Lead border: dark metallic with shockwave shimmer
    if (leadBorder > 0.01) {
        vec3 leadBaseCol = colorWithFallback(customColors[2].rgb, vec3(0.25, 0.22, 0.2));
        vec3 leadCol = leadBaseCol;
        // Approximate audio modulation for label context (shockwave + ripple)
        float shimmer = hasAudio ? bass * 0.6 * labelAudioMul : 0.0;
        leadCol += vec3(0.4, 0.35, 0.3) * shimmer;
        color.rgb = mix(color.rgb, leadCol, leadBorder * labelBright);
        color.a = max(color.a, leadBorder * 0.5);
    }

    // Tile fill: brightened hue center, posterized to match stained-glass look
    if (labels.a > 0.01) {
        vec3 labelHue = mix(hueCenter, shapeTint, 0.5) * 1.4;
        labelHue = floor(labelHue * posterLevels) / posterLevels;
        float flashIntensity = 1.0 + (hasAudio ? bass * 0.4 * labelAudioMul : 0.0);
        vec3 tileCol = labelHue * flashIntensity;
        color.rgb = mix(color.rgb, tileCol, labels.a * labelBright);
        color.a = max(color.a, labels.a);
    }

    return color;
}

// ─── Main ───────────────────────────────────────────────────────

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

        vec4 zoneColor = renderZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);

        color = blendOver(color, zoneColor);
    }

    if (customParams[3].z > 0.5)
        color = compositeMosaicLabels(color, fragCoord, bass, mids, treble, overall, hasAudio);
    fragColor = clampFragColor(color);
}
