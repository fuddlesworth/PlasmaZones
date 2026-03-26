// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Ember Trace — Effect pass (final composite)
//
// Reads the ping-pong feedback fire buffer (iChannel0) and composites
// fire-colored zones with chromatic aberration, zone resonance, rich animated
// borders, pattern-aware bloom, and audio-narrative behaviors.
//
// Buffer channels:
//   R = energy intensity (brightness of fire, 0-1)
//   G = heat/freshness (1 = just injected, decays faster for color aging)
//   B = flow speed (curl noise magnitude, for motion-aware coloring)
//   A = 1

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <audio.glsl>

// ─── Parameters ─────────────────────────────────────────────────────────────

float getFillOpacity()     { return customParams[2].x >= 0.0 ? customParams[2].x : 0.92; }
float getBloomStrength()   { return customParams[2].y >= 0.0 ? customParams[2].y : 0.4; }
float getZoneTint()        { return customParams[2].z >= 0.0 ? customParams[2].z : 0.15; }
float getChromaStrength()  { return customParams[2].w >= 0.0 ? customParams[2].w : 3.0; }

float getLabelGlowSpread() { return customParams[3].x >= 0.0 ? customParams[3].x : 3.0; }
float getLabelBrightness() { return customParams[3].y >= 0.0 ? customParams[3].y : 2.0; }
float getLabelAudioReact() { return customParams[3].z >= 0.0 ? customParams[3].z : 1.0; }

// ─── Fire color palette ─────────────────────────────────────────────────────
// Multi-ramp palette: maps heat (freshness) and flow speed to fire temperature.
// Fresh fire = white-hot core, aging = warm orange, old/cool trails = purple.
// Active flow regions get iridescent edge glow.

vec3 fireColor(float heat, float flowSpeed, float energy,
               vec3 hotCol, vec3 warmCol, vec3 coolCol, float midsShift) {
    // Heat drives the brightness ramp: cool → warm → hot → white (widened for brighter output)
    float t = smoothstep(0.01, 0.35, heat);
    vec3 col = coolCol * 0.35;
    col = mix(col, coolCol * 0.8, smoothstep(0.0, 0.1, t));
    col = mix(col, warmCol,       smoothstep(0.1, 0.35, t));
    col = mix(col, hotCol,        smoothstep(0.35, 0.65, t));
    col = mix(col, vec3(1.0),     smoothstep(0.65, 1.0, t)); // white-hot core

    // Flow speed drives iridescent shimmer at active boundaries
    // Thin-film interference approximation based on flow phase
    float flowPhase = flowSpeed * 12.0 + midsShift;
    vec3 irid = vec3(
        0.5 + 0.5 * cos(flowPhase),
        0.5 + 0.5 * cos(flowPhase + 2.094),
        0.5 + 0.5 * cos(flowPhase + 4.189)
    );
    float frontGlow = smoothstep(0.05, 0.4, flowSpeed);
    col += mix(hotCol, irid, 0.3) * frontGlow * 2.5;

    // Low energy darkens (depleted areas are voids in the fire)
    col *= mix(0.5, 1.0, smoothstep(0.05, 0.4, energy));

    return col;
}

// ─── Chromatic aberration ───────────────────────────────────────────────────
// Samples buffer at RGB-offset positions; direction rotates slowly,
// intensifies on bass via cascade wavefront.

vec3 sampleChromatic(vec2 fc, float chromaPx, float caAngle,
                     vec3 hotCol, vec3 warmCol, vec3 coolCol, float midsShift) {
    vec2 caDir = vec2(cos(caAngle), sin(caAngle));
    vec2 rOff = caDir * chromaPx;
    vec2 bOff = -caDir * chromaPx;

    // Clamp offset coords to prevent edge-wrapping artifacts (sampler is repeat)
    vec2 maxFc = iChannelResolution[0];
    vec4 sR = texture(iChannel0, channelUv(0, clamp(fc + rOff, vec2(0.0), maxFc)));
    vec4 sG = texture(iChannel0, channelUv(0, fc));
    vec4 sB = texture(iChannel0, channelUv(0, clamp(fc + bOff, vec2(0.0), maxFc)));

    // Buffer: R=energy, G=heat, B=flowSpeed
    vec3 cR = fireColor(sR.g, sR.b, sR.r, hotCol, warmCol, coolCol, midsShift);
    vec3 cG = fireColor(sG.g, sG.b, sG.r, hotCol, warmCol, coolCol, midsShift);
    vec3 cB = fireColor(sB.g, sB.b, sB.r, hotCol, warmCol, coolCol, midsShift);

    return vec3(cR.r, cG.g, cB.b);
}

// ─── Pattern-aware bloom ────────────────────────────────────────────────────
// Intensity-weighted: high-reaction regions bloom more, dead zones don't.

vec3 sampleBloom(vec2 fc, float radius,
                 vec3 hotCol, vec3 warmCol, vec3 coolCol, float midsShift) {
    // Scale radius by resolution for consistent visual size across DPI
    float scaledRadius = radius * max(iResolution.y, 1.0) / 1080.0;
    vec3 bloom = vec3(0.0);
    float totalW = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float gw = exp(-float(dx * dx + dy * dy) * 0.35);
            vec2 sc = fc + vec2(float(dx), float(dy)) * scaledRadius;
            vec4 s = texture(iChannel0, channelUv(0, sc));
            // Intensity-weight: reaction-active samples contribute more
            float intensity = s.g + s.b * 2.0;
            float w = gw * (0.3 + intensity);
            bloom += fireColor(s.g, s.b, s.r, hotCol, warmCol, coolCol, midsShift) * w;
            totalW += w;
        }
    }
    return bloom / max(totalW, 0.001);
}

// ─── Zone rendering ─────────────────────────────────────────────────────────

vec4 renderEmberZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                     vec4 params, bool isHighlighted,
                     vec3 hotCol, vec3 warmCol, vec3 coolCol,
                     float bass, float mids, float treble, bool hasAudio) {
    // Early bounding-box rejection: skip fragments beyond outer glow range
    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    float borderRadius = max(params.x, 8.0);
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    if (d > 30.0) return vec4(0.0); // beyond 24px glow + margin

    float borderWidth  = max(params.y, 2.0);
    float fillOpacity  = getFillOpacity();
    float bloomStr     = getBloomStrength();
    float zoneTint     = getZoneTint();

    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);
    vec4 result = vec4(0.0);

    vec3 borderClr = colorWithFallback(borderColor.rgb, vec3(0.9, 0.5, 0.2));
    float vitality = zoneVitality(isHighlighted);
    float angle = atan(p.y, p.x);

    // Audio envelopes
    float bassEnv = hasAudio ? bass * bass : 0.0;
    float radialPos = length(localUV - 0.5) * 2.0;

    // Mids-driven palette phase shift for color cycling
    float midsShift = hasAudio ? iTime * (0.3 + mids * 2.5) : iTime * 0.3;

    // ── Cascade chain-reaction chromatic ─────────────────────────────────
    // Bass triggers radial wavefront: center reacts first, edges with delay.
    float cascadeSignal = hasAudio
        ? smoothstep(radialPos - 0.15, radialPos, fract(iTime * 1.8))
          * exp(-fract(iTime * 1.8) * 2.5) * bassEnv
        : 0.0;
    float chromaPx = getChromaStrength() * (1.0 + cascadeSignal * 8.0);
    float caAngle = iTime * 0.5;

    // ── Zone resonance frequency ─────────────────────────────────────────
    // Each zone has a hash-based frequency; bass selectively excites matching zones.
    float nodeFreq = hash21(rectPos * 0.01);
    float resonance = hasAudio
        ? max(0.0, 1.0 - abs(nodeFreq - bass * 2.0) * 3.0) * bassEnv
        : 0.0;

    // ── Inside zone ─────────────────────────────────────────────────────
    if (d < 0.0) {
        // Chromatic-aberrated fire color
        vec3 col = sampleChromatic(fragCoord, chromaPx * vitalityScale(0.5, 1.5, vitality),
                                   caAngle, hotCol, warmCol, coolCol, midsShift);

        // Pattern-aware bloom: follows fire intensity, not uniform
        vec3 bloom = sampleBloom(fragCoord, 2.5, hotCol, warmCol, coolCol, midsShift);
        col = mix(col, bloom, bloomStr * 0.7);
        col += bloom * bloomStr * 0.3;

        // Zone fill tint
        col = mix(col, col * fillColor.rgb, zoneTint);

        // Vitality modulation
        col = vitalityDesaturate(col, vitality);
        col *= vitalityScale(0.5, 1.3, vitality);

        // ── Inner glow with exponential bevel from edge inward ──────────
        float innerDist = -d;
        float bevelGlow = exp(-innerDist / 16.0);
        float radialGlow = max(0.0, 1.0 - radialPos);
        float combinedGlow = mix(bevelGlow, radialGlow, 0.4);

        float pulseSpeed = vitalityScale(0.5, 3.0, vitality);
        float pulse = 0.5 + 0.5 * sin(iTime * pulseSpeed);
        float innerGlow = combinedGlow * pulse * vitalityScale(0.04, 0.2, vitality);

        // Resonance amplifies inner glow for matching zones
        innerGlow *= 1.0 + resonance * 3.0;

        // ── Mids-driven glow color cycling ──────────────────────────────
        // borderClr -> fillColor -> complementary, looping with mids intensity
        vec3 complementary = vec3(1.0) - borderClr;
        float cyclePhase = hasAudio ? fract(iTime * (0.3 + mids * 2.0)) : 0.0;
        vec3 glowClr;
        if (cyclePhase < 0.333) {
            glowClr = mix(borderClr, fillColor.rgb, cyclePhase * 3.0);
        } else if (cyclePhase < 0.666) {
            glowClr = mix(fillColor.rgb, complementary, (cyclePhase - 0.333) * 3.0);
        } else {
            glowClr = mix(complementary, borderClr, (cyclePhase - 0.666) * 3.0);
        }
        float cycleBlend = hasAudio ? smoothstep(0.05, 0.5, mids) : 0.0;
        vec3 finalGlowClr = mix(borderClr, glowClr, cycleBlend);

        // Warm shift on mids
        if (hasAudio && mids > 0.05) {
            finalGlowClr = mix(finalGlowClr, warmCol, mids * 0.4);
        }

        col += finalGlowClr * innerGlow;

        result.rgb = clamp(col, 0.0, 1.0);

        // Alpha from pattern intensity
        vec4 buf = texture(iChannel0, channelUv(0, fragCoord));
        float patternStr = smoothstep(0.02, 0.15, buf.g + buf.b * 0.5 + bloom.r * bloomStr);
        result.a = fillOpacity * max(patternStr, vitalityScale(0.0, 0.1, vitality));

        if (isHighlighted) {
            result.a = min(result.a + 0.06, 1.0);
        }
    }

    // ── Border: fire angular flow + bass pulse + pattern sampling ────────
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float flowSpeed = vitalityScale(0.3, 2.5, vitality);
        float flowRange = vitalityScale(0.1, 0.4, vitality);
        float flow = angularNoise(angle, 8.0, -iTime * flowSpeed) * flowRange
                   + (1.0 - flowRange * 0.5);

        // Bass: pulse racing around perimeter like a burning fuse
        float pulsePos = hasAudio ? fract(iTime * 2.0) * TAU : 0.0;
        float angDist = 1.0 - abs(mod(angle - pulsePos + PI, TAU) - PI) / PI;
        float borderEnergy = 1.0 + (hasAudio ? angDist * bass * 1.0 : 0.0);

        // Resonance also amplifies the border
        borderEnergy += resonance * 0.5;

        // Sample reaction pattern at border for color variation
        vec4 borderBuf = texture(iChannel0, channelUv(0, fragCoord));
        float borderHeat = borderBuf.g * 0.5 + borderBuf.b;
        vec3 borderFlow = mix(borderClr, warmCol, borderHeat * 0.5) * flow * borderEnergy;

        // Second pulse: offset counter-rotating for complexity
        if (hasAudio && bass > 0.1) {
            float pulse2Pos = fract(-iTime * 1.3 + 0.5) * TAU;
            float angDist2 = 1.0 - abs(mod(angle - pulse2Pos + PI, TAU) - PI) / PI;
            borderFlow += hotCol * angDist2 * bassEnv * 0.3;
        }

        if (isHighlighted) {
            float accentTrace = angularNoise(angle, 6.0, iTime * 2.5);
            borderFlow = mix(borderFlow, hotCol * borderEnergy, accentTrace * 0.3);
        }

        float borderAlpha = border * vitalityScale(0.8, 0.95, vitality);
        result.rgb = mix(result.rgb, borderFlow, borderAlpha);
        result.a = max(result.a, border * 0.96);
    }

    // ── Outer glow with expanding bass wavefronts ───────────────────────
    if (d > 0.0 && d < 24.0) {
        float glowRadius = vitalityScale(5.0, 10.0, vitality);
        float glowFalloff = vitalityScale(0.3, 0.6, vitality);

        // Bass: expanding ring wavefronts radiating from edge
        if (hasAudio) {
            float waveCycle = fract(iTime * 1.2);
            float waveRadius = waveCycle * 18.0;
            float waveBand = exp(-abs(d - waveRadius) * 0.5) * (1.0 - waveCycle);
            glowRadius += waveBand * bassEnv * 8.0;
            glowFalloff += waveBand * bass * 0.4;

            // Second wavefront offset in time
            float waveCycle2 = fract(iTime * 1.2 + 0.5);
            float waveRadius2 = waveCycle2 * 18.0;
            float waveBand2 = exp(-abs(d - waveRadius2) * 0.5) * (1.0 - waveCycle2);
            glowRadius += waveBand2 * bassEnv * 4.0;
        }

        float glow = expGlow(d, glowRadius, glowFalloff);
        glow *= vitalityScale(0.3, 1.0, vitality);

        // Resonance brightens glow for matching zones
        glow *= 1.0 + resonance * 1.5;

        vec3 glowColor = mix(warmCol, borderClr, 0.5);
        // Chromatic glow: slight color split in the outer glow
        vec3 outerGlowCol = vec3(
            expGlow(d - 1.0, glowRadius, glowFalloff),
            glow,
            expGlow(d + 1.0, glowRadius, glowFalloff)
        ) * glowColor;

        result.rgb += outerGlowCol;
        result.a = max(result.a, glow * 0.6);
    }

    // ── Treble: ember spark traces racing inner edge ────────────────────
    // Bright sparks travel the zone perimeter, picking up fire palette.
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

        // Sample buffer for spark color — sparks inherit the local fire color
        vec4 sparkBuf = texture(iChannel0, channelUv(0, fragCoord));
        vec3 sparkCol = mix(hotCol, vec3(1.0), sparkBuf.b * 0.5);
        result.rgb += sparkCol * edgeProx * spark * treble * 0.6;
    }

    return result;
}

// ─── Label compositing ──────────────────────────────────────────────────────
// Pattern-aware: samples fire buffer near labels for color tinting.
// Chromatic-split halo, treble sparkle, bass-pulsed core.

vec4 compositeEmberLabels(vec4 color, vec2 fragCoord,
                          float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    float glowSpread = getLabelGlowSpread();
    float brightness = getLabelBrightness();
    float audioReact = getLabelAudioReact();

    // Chromatic aberration on labels — direction rotates with time
    float caAngle = iTime * 0.7;
    vec2 caDir = vec2(cos(caAngle), sin(caAngle));
    // Offset in UV space (px.x * iResolution.x cancels to 1.0)
    float caAmount = (hasAudio ? 2.0 + bass * 3.0 * audioReact : 2.0) * 0.003;

    float rCh = texture(uZoneLabels, uv + caDir * caAmount).a;
    float gCh = labels.a;
    float bCh = texture(uZoneLabels, uv - caDir * caAmount).a;

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

    // Fire-colored glow halo tinted by nearby reaction pattern
    if (halo > 0.003) {
        float haloEdge = halo * (1.0 - max(max(rCh, gCh), bCh));

        vec3 glowClr = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.33, 0.13));
        vec3 hotClr  = colorWithFallback(customColors[0].rgb, vec3(1.0, 0.8, 0.25));

        // Sample reaction buffer for color variation
        vec4 nearBuf = texture(iChannel0, channelUv(0, fragCoord));
        float bufHeat = nearBuf.g * 0.5 + nearBuf.b;
        glowClr = mix(glowClr, hotClr, bufHeat * 0.5);

        // Signal traces: pulses traveling along the halo outline
        float labelAngle = atan(uv.y - 0.5, uv.x - 0.5);
        float signal = smoothstep(0.8, 1.0, sin(labelAngle * 10.0 - iTime * 5.0));
        float traceGlow = haloEdge * (0.3 + signal * 0.7);

        float haloBright = traceGlow * (0.5 + (hasAudio ? bass * 0.6 * audioReact : 0.0));
        color.rgb += glowClr * haloBright;

        // Treble sparkles at label edges
        if (hasAudio && treble > 0.1) {
            float sparkNoise = noise2D(uv * 50.0 + iTime * 3.0);
            float spark = smoothstep(0.7, 0.95, sparkNoise) * treble * 2.5 * audioReact;
            vec3 sparkCol = mix(glowClr, vec3(1.0), 0.4);
            color.rgb += sparkCol * haloEdge * spark;
        }

        color.a = max(color.a, haloEdge * 0.5);
    }

    // Chromatic-split core label
    float maxCh = max(max(rCh, gCh), bCh);
    if (maxCh > 0.01) {
        vec3 chromaticLabel = vec3(rCh, gCh, bCh);
        float boost = brightness * (hasAudio ? 1.0 + bass * 0.4 * audioReact : 1.0);
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

    vec3 hotCol  = colorWithFallback(customColors[0].rgb, vec3(1.0, 0.8, 0.25));
    vec3 warmCol = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.33, 0.13));
    vec3 coolCol = colorWithFallback(customColors[2].rgb, vec3(0.4, 0.13, 0.8));

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass   = getBassSoft();
    float mids   = getMidsSoft();
    float treble = getTrebleSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0)
            continue;
        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderEmberZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            hotCol, warmCol, coolCol, bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (customParams[3].w > 0.5)
        color = compositeEmberLabels(color, fragCoord, bass, mids, treble, hasAudio);

    fragColor = clampFragColor(color);
}
