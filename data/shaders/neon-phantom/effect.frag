// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Neon Phantom — Effect pass (final composite)
//
// Reads the ping-pong feedback phantom buffer (iChannel0) and composites
// neon-colored zones with cyberpunk post-processing: scanlines, chromatic
// aberration, holographic interference, glitch effects, and data flow patterns.
//
// Buffer channels:
//   R = phantom energy intensity (0-1)
//   G = freshness/heat (1 = just emitted, decays for color aging)
//   B = hex proximity (distance to hex lattice for structural coloring)
//   A = 1

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <audio.glsl>

// ─── Parameters ─────────────────────────────────────────────────────────────

float getFillOpacity()     { return customParams[2].x >= 0.0 ? customParams[2].x : 0.88; }
float getBloomStrength()   { return customParams[2].y >= 0.0 ? customParams[2].y : 0.5; }
float getZoneTint()        { return customParams[2].z >= 0.0 ? customParams[2].z : 0.2; }
float getChromaStrength()  { return customParams[2].w >= 0.0 ? customParams[2].w : 4.0; }

float getScanlineStr()     { return customParams[3].x >= 0.0 ? customParams[3].x : 0.04; }
float getGlitchRate()      { return customParams[3].y >= 0.0 ? customParams[3].y : 0.015; }
float getLabelGlowSpread() { return customParams[3].z >= 0.0 ? customParams[3].z : 3.0; }

// ─── Neon phantom color palette ─────────────────────────────────────────────
// Maps energy intensity, freshness, and hex proximity to Catppuccin neon colors.
// Fresh energy = white-hot cyan core, aging = pink/mauve, old trails = deep purple.
// Hex lattice proximity adds structural cyan highlighting.

vec3 phantomColor(float energy, float heat, float hexProx,
                  vec3 cyanCol, vec3 pinkCol, vec3 purpleCol, float midsShift) {
    float t = smoothstep(0.01, 0.35, energy);

    // Base: visible purple even at low energy
    vec3 col = purpleCol * 0.35;
    col = mix(col, purpleCol * 0.8, smoothstep(0.0, 0.1, t));
    col = mix(col, pinkCol,         smoothstep(0.1, 0.3, t));
    col = mix(col, cyanCol * 1.2,   smoothstep(0.3, 0.6, t));
    col = mix(col, vec3(0.9, 1.0, 1.0), smoothstep(0.6, 1.0, t)); // white-hot neon core

    // Hex lattice structure: bright cyan highlights along hex edges
    float hexGlow = hexProx * smoothstep(0.03, 0.2, energy);
    col += cyanCol * hexGlow * 0.8;

    // Freshness iridescence (thin-film interference, cyberpunk shimmer)
    float phase = heat * 10.0 + midsShift;
    vec3 irid = vec3(
        0.5 + 0.5 * cos(phase),
        0.5 + 0.5 * cos(phase + 2.094),
        0.5 + 0.5 * cos(phase + 4.189)
    );
    float frontGlow = smoothstep(0.05, 0.35, heat);
    col += mix(cyanCol, irid, 0.35) * frontGlow * 2.2;

    return col;
}

// ─── Chromatic aberration ───────────────────────────────────────────────────

vec3 sampleChromatic(vec2 fc, float chromaPx, float caAngle,
                     vec3 cyanCol, vec3 pinkCol, vec3 purpleCol, float midsShift) {
    vec2 caDir = vec2(cos(caAngle), sin(caAngle));
    vec2 rOff = caDir * chromaPx;
    vec2 bOff = -caDir * chromaPx;

    vec4 sR = texture(iChannel0, channelUv(0, fc + rOff));
    vec4 sG = texture(iChannel0, channelUv(0, fc));
    vec4 sB = texture(iChannel0, channelUv(0, fc + bOff));

    vec3 cR = phantomColor(sR.r, sR.g, sR.b, cyanCol, pinkCol, purpleCol, midsShift);
    vec3 cG = phantomColor(sG.r, sG.g, sG.b, cyanCol, pinkCol, purpleCol, midsShift);
    vec3 cB = phantomColor(sB.r, sB.g, sB.b, cyanCol, pinkCol, purpleCol, midsShift);

    return vec3(cR.r, cG.g, cB.b);
}

// ─── Pattern-aware bloom ────────────────────────────────────────────────────

vec3 sampleBloom(vec2 fc, float radius,
                 vec3 cyanCol, vec3 pinkCol, vec3 purpleCol, float midsShift) {
    vec3 bloom = vec3(0.0);
    float totalW = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float gw = exp(-float(dx * dx + dy * dy) * 0.35);
            vec2 sc = fc + vec2(float(dx), float(dy)) * radius;
            vec4 s = texture(iChannel0, channelUv(0, sc));
            float intensity = s.r + s.g * 2.0;
            float w = gw * (0.3 + intensity);
            bloom += phantomColor(s.r, s.g, s.b, cyanCol, pinkCol, purpleCol, midsShift) * w;
            totalW += w;
        }
    }
    return bloom / max(totalW, 0.001);
}

// ─── Zone rendering ─────────────────────────────────────────────────────────

vec4 renderPhantomZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                       vec4 params, bool isHighlighted,
                       vec3 cyanCol, vec3 pinkCol, vec3 purpleCol,
                       float bass, float mids, float treble, bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth  = max(params.y, 2.0);
    float fillOpacity  = getFillOpacity();
    float bloomStr     = getBloomStrength();
    float zoneTint     = getZoneTint();
    float scanlineStr  = getScanlineStr();
    float glitchRate   = getGlitchRate();

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    vec4 result = vec4(0.0);

    vec3 borderClr = colorWithFallback(borderColor.rgb, vec3(0.545, 0.835, 0.792));
    float vitality = zoneVitality(isHighlighted);
    float angle = atan(p.y, p.x);

    float bassEnv = hasAudio ? bass * bass : 0.0;
    float radialPos = length(localUV - 0.5) * 2.0;
    float midsShift = hasAudio ? iTime * (0.3 + mids * 2.0) : iTime * 0.3;

    // ── Cascade chromatic ───────────────────────────────────────────────
    float cascadeSignal = hasAudio
        ? smoothstep(radialPos - 0.15, radialPos, fract(iTime * 1.5))
          * exp(-fract(iTime * 1.5) * 2.5) * bassEnv
        : 0.0;
    float chromaPx = getChromaStrength() * (1.0 + cascadeSignal * 6.0);
    float caAngle = iTime * 0.5 + sin(iTime * 3.0) * 0.3; // neon_phantom pulsing CA

    // ── Zone resonance ──────────────────────────────────────────────────
    float nodeFreq = hash21(rectPos * 0.01);
    float resonance = hasAudio
        ? max(0.0, 1.0 - abs(nodeFreq - bass * 2.0) * 3.0) * bassEnv
        : 0.0;

    // ── Inside zone ─────────────────────────────────────────────────────
    if (d < 0.0) {
        vec3 col = sampleChromatic(fragCoord, chromaPx * vitalityScale(0.5, 1.5, vitality),
                                   caAngle, cyanCol, pinkCol, purpleCol, midsShift);

        vec3 bloom = sampleBloom(fragCoord, 2.5, cyanCol, pinkCol, purpleCol, midsShift);
        col = mix(col, bloom, bloomStr * 0.6);
        col += bloom * bloomStr * 0.35;

        // Zone fill tint
        col = mix(col, col * fillColor.rgb, zoneTint);

        col = vitalityDesaturate(col, vitality);
        col *= vitalityScale(0.5, 1.3, vitality);

        // ── Inner phantom glow ──────────────────────────────────────────
        float innerDist = -d;
        float bevelGlow = exp(-innerDist / 18.0);
        float radialGlow = max(0.0, 1.0 - radialPos);
        float combinedGlow = mix(bevelGlow, radialGlow, 0.35);

        float pulseSpeed = vitalityScale(0.4, 2.5, vitality);
        float pulse = 0.5 + 0.5 * sin(iTime * pulseSpeed);
        float innerGlow = combinedGlow * pulse * vitalityScale(0.03, 0.18, vitality);
        innerGlow *= 1.0 + resonance * 2.5;

        // ── Mids color cycling (neon palette rotation) ──────────────────
        vec3 complementary = vec3(1.0) - borderClr;
        float cyclePhase = hasAudio ? fract(iTime * (0.25 + mids * 1.5)) : 0.0;
        vec3 glowClr;
        if (cyclePhase < 0.333) {
            glowClr = mix(cyanCol, pinkCol, cyclePhase * 3.0);
        } else if (cyclePhase < 0.666) {
            glowClr = mix(pinkCol, purpleCol, (cyclePhase - 0.333) * 3.0);
        } else {
            glowClr = mix(purpleCol, cyanCol, (cyclePhase - 0.666) * 3.0);
        }
        float cycleBlend = hasAudio ? smoothstep(0.05, 0.5, mids) : 0.0;
        vec3 finalGlowClr = mix(borderClr, glowClr, max(cycleBlend, 0.3));

        col += finalGlowClr * innerGlow;

        // ── Cyberpunk scanlines ─────────────────────────────────────────
        float scanline = sin(fragCoord.y * 1.0 - iTime * 2.0);
        col *= 1.0 - scanlineStr * scanline;

        // ── Holographic interference ────────────────────────────────────
        float interference = sin(fragCoord.y * 0.15 + iTime * 5.0);
        col += vec3(0.005, 0.008, 0.012) * interference;

        // ── Glitch effect ───────────────────────────────────────────────
        float glitchTime = floor(iTime * 4.0);
        float glitchRand = hash21(vec2(glitchTime, floor(localUV.y * 30.0)));
        if (glitchRand > 1.0 - glitchRate) {
            col.r += 0.06;
            col.b += 0.06;
        }

        result.rgb = clamp(col, 0.0, 1.0);

        vec4 buf = texture(iChannel0, channelUv(0, fragCoord));
        float patternStr = smoothstep(0.02, 0.15, buf.r + buf.g * 0.5 + bloom.r * bloomStr);
        result.a = fillOpacity * max(patternStr, vitalityScale(0.0, 0.1, vitality));

        if (isHighlighted) {
            result.a = min(result.a + 0.06, 1.0);
        }
    }

    // ── Border: neon flow + phantom pulse ────────────────────────────────
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float flowSpeed = vitalityScale(0.3, 2.0, vitality);
        float flowRange = vitalityScale(0.1, 0.4, vitality);
        float flow = angularNoise(angle, 8.0, -iTime * flowSpeed) * flowRange
                   + (1.0 - flowRange * 0.5);

        // Phantom pulse racing the perimeter
        float pulsePos = hasAudio ? fract(iTime * 1.8) * TAU : fract(iTime * 0.8) * TAU;
        float angDist = 1.0 - abs(mod(angle - pulsePos + PI, TAU) - PI) / PI;
        float borderEnergy = 1.0 + (hasAudio ? angDist * bass * 0.8 : angDist * 0.15);
        borderEnergy += resonance * 0.4;

        // Neon-colored border using buffer data
        vec4 borderBuf = texture(iChannel0, channelUv(0, fragCoord));
        float bufHeat = borderBuf.g * 0.5 + borderBuf.b;
        vec3 borderFlow = mix(borderClr, cyanCol, bufHeat * 0.4) * flow * borderEnergy;

        // Counter-rotating phantom pulse
        if (hasAudio && bass > 0.1) {
            float pulse2Pos = fract(-iTime * 1.2 + 0.5) * TAU;
            float angDist2 = 1.0 - abs(mod(angle - pulse2Pos + PI, TAU) - PI) / PI;
            borderFlow += pinkCol * angDist2 * bassEnv * 0.25;
        }

        if (isHighlighted) {
            float accentTrace = angularNoise(angle, 6.0, iTime * 2.5);
            borderFlow = mix(borderFlow, cyanCol * borderEnergy, accentTrace * 0.3);
        }

        // Scanline on border
        float borderScan = sin(fragCoord.y * 1.0 - iTime * 2.0);
        borderFlow *= 1.0 - getScanlineStr() * borderScan * 0.5;

        float borderAlpha = border * vitalityScale(0.8, 0.95, vitality);
        result.rgb = mix(result.rgb, borderFlow, borderAlpha);
        result.a = max(result.a, border * 0.96);
    }

    // ── Outer glow: neon phantom aura ───────────────────────────────────
    if (d > 0.0 && d < 24.0) {
        float glowRadius = vitalityScale(5.0, 10.0, vitality);
        float glowFalloff = vitalityScale(0.3, 0.55, vitality);

        if (hasAudio) {
            float waveCycle = fract(iTime * 1.0);
            float waveRadius = waveCycle * 18.0;
            float waveBand = exp(-abs(d - waveRadius) * 0.5) * (1.0 - waveCycle);
            glowRadius += waveBand * bassEnv * 6.0;
            glowFalloff += waveBand * bass * 0.3;

            float waveCycle2 = fract(iTime * 1.0 + 0.5);
            float waveRadius2 = waveCycle2 * 18.0;
            float waveBand2 = exp(-abs(d - waveRadius2) * 0.5) * (1.0 - waveCycle2);
            glowRadius += waveBand2 * bassEnv * 3.0;
        }

        float glow = expGlow(d, glowRadius, glowFalloff);
        glow *= vitalityScale(0.3, 1.0, vitality);
        glow *= 1.0 + resonance * 1.2;

        // Neon chromatic glow (RGB-split outer aura)
        vec3 glowColor = mix(purpleCol, cyanCol, 0.5);
        vec3 outerGlowCol = vec3(
            expGlow(d - 1.0, glowRadius, glowFalloff),
            glow,
            expGlow(d + 1.0, glowRadius, glowFalloff)
        ) * glowColor;

        result.rgb += outerGlowCol;
        result.a = max(result.a, glow * 0.5);
    }

    // ── Treble: phantom spark traces ────────────────────────────────────
    if (hasAudio && treble > 0.06 && d > -borderWidth * 4.0 && d < 0.0) {
        float edgeProx = smoothstep(-borderWidth * 4.0, -borderWidth * 0.5, d);
        float spark = 0.0;
        for (int si = 0; si < 4; si++) {
            float sparkSpeed = 2.5 + float(si) * 1.4;
            float sparkPos = fract(iTime * sparkSpeed * 0.3 + float(si) * 0.25);
            float sparkAngle = sparkPos * TAU;
            float aDist = abs(mod(angle - sparkAngle + PI, TAU) - PI);
            float sparkBright = exp(-aDist * 10.0) * (0.5 + 0.5 * sin(iTime * sparkSpeed * 1.5));
            spark += sparkBright;
        }
        spark = min(spark, 1.5);

        // Neon-colored sparks
        vec3 sparkCol = mix(cyanCol, vec3(1.0), 0.3);
        result.rgb += sparkCol * edgeProx * spark * treble * 0.5;
    }

    return result;
}

// ─── Label compositing ──────────────────────────────────────────────────────

vec4 compositePhantomLabels(vec4 color, vec2 fragCoord,
                            vec3 cyanCol, vec3 pinkCol,
                            float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    float glowSpread = getLabelGlowSpread();

    // Chromatic aberration on labels
    float caAngle = iTime * 0.7;
    vec2 caDir = vec2(cos(caAngle), sin(caAngle));
    float caAmount = (hasAudio ? 2.0 + bass * 2.5 : 2.0) * px.x * iResolution.x * 0.003;

    float rCh = texture(uZoneLabels, uv + caDir * caAmount).a;
    float gCh = labels.a;
    float bCh = texture(uZoneLabels, uv - caDir * caAmount).a;

    // Gaussian halo
    float halo = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.3);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * px * glowSpread).a * w;
        }
    }
    halo /= 9.51;

    // Neon-colored glow halo
    if (halo > 0.003) {
        float haloEdge = halo * (1.0 - max(max(rCh, gCh), bCh));

        vec3 glowClr = mix(cyanCol, pinkCol, sin(iTime * 2.0) * 0.5 + 0.5);
        float haloBright = haloEdge * (0.4 + (hasAudio ? bass * 0.5 : 0.0));
        color.rgb += glowClr * haloBright;

        // Treble sparkles at label edges
        if (hasAudio && treble > 0.1) {
            float sparkNoise = noise2D(uv * 50.0 + iTime * 3.0);
            float spark = smoothstep(0.7, 0.95, sparkNoise) * treble * 2.0;
            color.rgb += cyanCol * haloEdge * spark;
        }

        color.a = max(color.a, haloEdge * 0.5);
    }

    // Chromatic-split core label
    float maxCh = max(max(rCh, gCh), bCh);
    if (maxCh > 0.01) {
        vec3 chromaticLabel = vec3(rCh, gCh, bCh);
        float boost = 2.0 * (hasAudio ? 1.0 + bass * 0.3 : 1.0);
        vec3 boosted = color.rgb * boost + chromaticLabel * 0.4;
        color.rgb = mix(color.rgb, boosted, maxCh);
        color.a = max(color.a, maxCh);
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

    vec3 cyanCol   = colorWithFallback(customColors[0].rgb, vec3(0.545, 0.835, 0.792));
    vec3 pinkCol   = colorWithFallback(customColors[1].rgb, vec3(0.961, 0.741, 0.902));
    vec3 purpleCol = colorWithFallback(customColors[2].rgb, vec3(0.776, 0.631, 0.957));

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass   = getBassSoft();
    float mids   = getMidsSoft();
    float treble = getTrebleSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0)
            continue;
        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderPhantomZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            cyanCol, pinkCol, purpleCol, bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (customParams[3].w > 0.5)
        color = compositePhantomLabels(color, fragCoord, cyanCol, pinkCol,
                                       bass, mids, treble, hasAudio);

    fragColor = clampFragColor(color);
}
