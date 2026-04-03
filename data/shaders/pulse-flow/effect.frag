// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Pulse Flow — Effect pass (final composite)
//
// Lightweight composite with full audio reactivity. Direct buffer color mapping
// (no bloom, no chromatic aberration), flowing zone borders, bass glow
// wavefronts, mids-driven color cycling, and treble spark traces.
//
// Per-zone cost: ~3 texture reads + palette math (vs 34+ in ember-trace).

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <audio.glsl>

// ─── Parameters ─────────────────────────────────────────────────────────────

float getAudioReact()   { return customParams[1].z >= 0.0 ? customParams[1].z : 1.0; }
float getFillOpacity()  { return customParams[2].x >= 0.0 ? customParams[2].x : 0.85; }
float getZoneTint()     { return customParams[2].y >= 0.0 ? customParams[2].y : 0.15; }
float getGlowRadius()   { return customParams[2].z >= 0.0 ? customParams[2].z : 8.0; }

// ─── Color palette ──────────────────────────────────────────────────────────
// 2-color gradient: hot glow → cool trail, driven by heat (freshness).

vec3 energyColor(float energy, float heat, vec3 glowCol, vec3 trailCol) {
    float t = smoothstep(0.02, 0.5, heat);
    vec3 col = trailCol * 0.3;
    col = mix(col, trailCol,  smoothstep(0.0, 0.2, t));
    col = mix(col, glowCol,   smoothstep(0.2, 0.6, t));
    col = mix(col, vec3(1.0), smoothstep(0.7, 1.0, t));
    col *= mix(0.4, 1.0, smoothstep(0.03, 0.3, energy));
    return col;
}

// ─── Zone rendering ─────────────────────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted,
                vec3 glowCol, vec3 trailCol,
                float bass, float mids, float treble, bool hasAudio) {
    float px = pxScale();
    float audioR = getAudioReact();

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;

    float borderRadius = max(params.x, 8.0) * px;
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    if (d > 20.0 * px) return vec4(0.0);

    float borderWidth = max(params.y, 2.0) * px;
    float fillOpacity = getFillOpacity();

    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    float vitality = zoneVitality(isHighlighted);
    float angle = atan(p.y, p.x);
    float radialPos = length(localUV - 0.5) * 2.0;
    float bassEnv = hasAudio ? bass * bass : 0.0;
    vec4 result = vec4(0.0);

    vec3 borderClr = colorWithFallback(borderColor.rgb, glowCol);

    // ── Mids-driven color cycling ───────────────────────────────────────
    // Smoothly rotate between glow and trail colors on mid-range energy.
    vec3 activeGlow = glowCol;
    if (hasAudio && mids > 0.04) {
        float cyclePhase = fract(iTime * (0.2 + mids * 1.5));
        vec3 cycledCol;
        if (cyclePhase < 0.5) {
            cycledCol = mix(glowCol, trailCol, cyclePhase * 2.0);
        } else {
            cycledCol = mix(trailCol, glowCol, (cyclePhase - 0.5) * 2.0);
        }
        activeGlow = mix(glowCol, cycledCol, smoothstep(0.04, 0.4, mids));
    }

    // ── Inside zone: direct buffer color ────────────────────────────────
    if (d < 0.0) {
        vec4 buf = texture(iChannel0, channelUv(0, fragCoord));
        vec3 col = energyColor(buf.r, buf.g, activeGlow, trailCol);

        // Zone fill tint
        col = mix(col, col * fillColor.rgb, getZoneTint());

        // Vitality modulation
        col = vitalityDesaturate(col, vitality);
        col *= vitalityScale(0.5, 1.2, vitality);

        // Inner pulse — bass amplifies it
        float pulseSpeed = vitalityScale(0.5, 2.0, vitality);
        float pulse = 0.5 + 0.5 * sin(iTime * pulseSpeed);
        float glowStr = vitalityScale(0.02, 0.1, vitality);
        if (hasAudio) {
            glowStr += bassEnv * audioR * 0.15;
        }
        float innerGlow = max(0.0, 1.0 - radialPos) * pulse * glowStr;
        col += activeGlow * innerGlow;

        // ── Treble: spark traces near inner edge ────────────────────────
        if (hasAudio && treble > 0.06 && d > -borderWidth * 3.0) {
            float edgeProx = smoothstep(-borderWidth * 3.0, -borderWidth * 0.5, d);
            float spark = 0.0;
            for (int si = 0; si < 3; si++) {
                float spd = 2.0 + float(si) * 1.5;
                float sparkAngle = fract(iTime * spd * 0.3 + float(si) * 0.333) * TAU;
                float aDist = abs(mod(angle - sparkAngle + PI, TAU) - PI);
                spark += exp(-aDist * 8.0) * (0.5 + 0.5 * sin(iTime * spd));
            }
            spark = min(spark, 1.0);
            col += activeGlow * edgeProx * spark * treble * audioR * 0.5;
        }

        result.rgb = clamp(col, 0.0, 1.0);

        float patternStr = smoothstep(0.02, 0.12, buf.r + buf.g * 0.5);
        result.a = fillOpacity * max(patternStr, vitalityScale(0.0, 0.08, vitality));
        if (isHighlighted) result.a = min(result.a + 0.05, 1.0);
    }

    // ── Border: flowing angular noise + bass perimeter pulse ────────────
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float flowSpeed = vitalityScale(0.3, 1.8, vitality);
        float flow = angularNoise(angle, 6.0, -iTime * flowSpeed) * 0.3 + 0.7;

        float borderEnergy = 1.0;
        // Bass: pulse racing around perimeter
        if (hasAudio && bass > 0.05) {
            float pulsePos = fract(iTime * 1.5) * TAU;
            float angDist = 1.0 - abs(mod(angle - pulsePos + PI, TAU) - PI) / PI;
            borderEnergy += angDist * bass * audioR * 1.0;

            // Counter-rotating second pulse for complexity
            float pulse2 = fract(-iTime * 1.1 + 0.5) * TAU;
            float angDist2 = 1.0 - abs(mod(angle - pulse2 + PI, TAU) - PI) / PI;
            borderEnergy += angDist2 * bassEnv * audioR * 0.4;
        }

        // Sample buffer at border for color variation
        vec4 borderBuf = texture(iChannel0, channelUv(0, fragCoord));
        float bufHeat = borderBuf.g * 0.5;
        vec3 borderFlow = mix(borderClr, activeGlow, bufHeat) * flow * borderEnergy;

        // Mids: warm tint shift on borders
        if (hasAudio && mids > 0.05) {
            borderFlow = mix(borderFlow, activeGlow * borderEnergy, mids * audioR * 0.3);
        }

        result.rgb = mix(result.rgb, borderFlow, border * vitalityScale(0.8, 0.95, vitality));
        result.a = max(result.a, border * 0.95);
    }

    // ── Outer glow with bass wavefronts ─────────────────────────────────
    float maxGlow = getGlowRadius();
    if (d > 0.0 && d < maxGlow * 2.5 * px) {
        float glowRadius = vitalityScale(maxGlow * 0.5, maxGlow, vitality) * px;
        float glowFalloff = vitalityScale(0.25, 0.5, vitality);

        // Bass: expanding wavefront from zone edge
        if (hasAudio && bass > 0.06) {
            float waveCycle = fract(iTime * 1.0);
            float waveRadius = waveCycle * 14.0 * px;
            float waveBand = exp(-abs(d - waveRadius) * (0.5 / px)) * (1.0 - waveCycle);
            glowRadius += waveBand * bassEnv * 5.0 * px;
        }

        float glow = expGlow(d, glowRadius, glowFalloff);
        glow *= vitalityScale(0.3, 1.0, vitality);

        result.rgb += mix(trailCol, activeGlow, 0.4) * glow;
        result.a = max(result.a, glow * 0.5);
    }

    return result;
}

// ─── Energy Label Composite ─────────────────────────────────────────────────
// Labels rendered as hot energy traces with flowing pulse halo, chromatic heat
// shift, bass-reactive brightness, and treble spark discharge along outlines.

float getLabelGlowSpread() { return customParams[3].x >= 0.0 ? customParams[3].x : 3.5; }
float getLabelBrightness() { return customParams[3].y >= 0.0 ? customParams[3].y : 1.5; }
float getLabelAudioReact() { return customParams[3].z >= 0.0 ? customParams[3].z : 1.0; }
float getLabelChromaStr()  { return customParams[3].w >= 0.0 ? customParams[3].w : 1.5; }

vec4 compositePulseLabels(vec4 color, vec2 fragCoord,
                          vec3 glowCol, vec3 trailCol,
                          float bass, float mids, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 texel = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    float labelGlowSpread = getLabelGlowSpread();
    float labelBrightness = getLabelBrightness();
    float labelAudioReact = getLabelAudioReact();
    float labelChromaStr  = getLabelChromaStr();

    // ── Halo: Gaussian blur of label alpha for glow envelope ────────────
    float halo = 0.0;
    float spread = labelGlowSpread + (hasAudio ? bass * 2.0 * labelAudioReact : 0.0);
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.25);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * texel * spread).a * w;
        }
    }
    halo /= 16.5;

    // ── Halo rendering: energy trail around text ────────────────────────
    if (halo > 0.003) {
        float haloEdge = halo * (1.0 - labels.a);

        // Flowing color: pulse between glow and trail along a radial wave
        float flowPhase = length(uv - 0.5) * 6.0 + iTime * 1.2;
        vec3 haloCol = mix(trailCol, glowCol, 0.5 + 0.5 * sin(flowPhase));

        // Mids shift the halo palette — warmer at high mids
        if (hasAudio && mids > 0.04) {
            float warmth = smoothstep(0.04, 0.4, mids);
            haloCol = mix(haloCol, glowCol * 1.3, warmth * 0.5);
        }

        float haloBright = haloEdge * 0.4 * labelBrightness;

        // Bass: halo pulses brighter on beat
        if (hasAudio) {
            haloBright *= 1.0 + bass * 1.2 * labelAudioReact;
        }

        // Treble: spark discharge along halo outline
        if (hasAudio && treble > 0.05 && labelAudioReact > 0.01) {
            float sparkAngle = atan(uv.y - 0.5, uv.x - 0.5);
            float sparkTravel = fract(iTime * 3.0) * TAU;
            float sparkDist = abs(mod(sparkAngle - sparkTravel + PI, TAU) - PI);
            float spark = exp(-sparkDist * 5.0) * treble * labelAudioReact;
            color.rgb += glowCol * haloEdge * spark * labelBrightness;
        }

        color.rgb += haloCol * haloBright;
        color.a = max(color.a, haloEdge * 0.5);
    }

    // ── Core text: hot energy with chromatic heat shift ──────────────────
    if (labels.a > 0.01) {
        // Chromatic aberration: offset R and B channels for heat distortion
        float caStr = (hasAudio ? labelChromaStr + bass * 2.5 * labelAudioReact : labelChromaStr)
            * texel.x * iResolution.x * 0.003;
        float caAngle = iTime * 0.9;
        vec2 caDir = vec2(cos(caAngle), sin(caAngle));
        float rCh = texture(uZoneLabels, uv + caDir * caStr).a;
        float bCh = texture(uZoneLabels, uv - caDir * caStr).a;

        // Compose: R from offset, G from center, B from opposite offset
        vec3 chromaticText = vec3(rCh, labels.a, bCh);

        // Color: blend between glow (hot) and white (overdriven) based on
        // heat, with trail color tinting the chromatic fringe
        vec3 core = mix(color.rgb * (1.0 + labelBrightness * 0.5), glowCol, 0.4)
            + chromaticText * trailCol * 0.3;
        core += glowCol * labels.a * 0.3 * labelBrightness;

        // Data flicker: irregular brightness variation (not a steady pulse)
        float flicker = 0.9 + 0.1 * sin(iTime * 19.0) * sin(iTime * 27.0);
        core *= flicker;

        // Bass: brightness surge
        if (hasAudio) {
            core *= 1.0 + bass * 0.6 * labelAudioReact;
        }

        color.rgb = mix(color.rgb, core, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}

// ─── Main ───────────────────────────────────────────────────────────────────

void main() {
    vec2 fragCoord = vFragCoord;

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 glowCol  = colorWithFallback(customColors[0].rgb, vec3(1.0, 0.6, 0.2));
    vec3 trailCol = colorWithFallback(customColors[1].rgb, vec3(0.4, 0.2, 0.8));

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass   = getBassSoft();
    float mids   = getMidsSoft();
    float treble = getTrebleSoft();

    vec4 color = vec4(0.0);
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            glowCol, trailCol, bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (customParams[1].w > 0.5) {
        color = compositePulseLabels(color, fragCoord, glowCol, trailCol,
                                     bass, mids, treble, hasAudio);
    }

    fragColor = clampFragColor(color);
}
