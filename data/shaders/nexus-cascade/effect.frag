// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Nexus Cascade — Multi-pass multi-channel zone overlay
// Pass 0: plasma base → iChannel0
// Pass 1: distorted + scanline layer → iChannel1
// Pass 2: bloom combine → iChannel2
// This pass: zone mask, chromatic blend, labels.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <audio.glsl>



// ─── Parameters ───────────────────────────────────────────────────

float getChromaStrength() { return customParams[3].x >= 0.0 ? customParams[3].x : 4.0; }
float getFillOpacity()    { return customParams[3].y >= 0.0 ? customParams[3].y : 0.92; }
float getChannelMix()     { return customParams[3].z >= 0.0 ? customParams[3].z : 0.5; }
float getZoneFillTint()  { return customParams[3].w >= 0.0 ? customParams[3].w : 0.0; }
float getAudioReact()    { return customParams[0].z >= 0.0 ? customParams[0].z : 1.0; }
float getBassChromaMul() { return customParams[2].w >= 0.0 ? customParams[2].w : 3.0; }


vec4 sampleNexus(vec2 fragCoord, vec2 uv, float chroma) {
    vec4 c0 = texture(iChannel0, channelUv(0, fragCoord));
    vec4 c1 = texture(iChannel1, channelUv(1, fragCoord));
    vec4 c2 = texture(iChannel2, channelUv(2, fragCoord));

    if (chroma > 0.5) {
        vec2 rOffPx = vec2(chroma, 0.0);
        vec2 bOffPx = vec2(-chroma, 0.0);
        float r = texture(iChannel2, channelUv(2, fragCoord + rOffPx)).r;
        float g = c2.g;
        float b = texture(iChannel2, channelUv(2, fragCoord + bOffPx)).b;
        c2 = vec4(r, g, b, 1.0);
    }

    float mixVal = getChannelMix();
    vec3 col = mix(c2.rgb, mix(c0.rgb, c1.rgb, 0.5), mixVal * 0.5);
    return vec4(clamp(col, 0.0, 1.0), 1.0);
}

vec4 renderNexusZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                     vec4 params, bool isHighlighted,
                     float bass, float mids, float treble, bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);
    float fillOpacity = getFillOpacity();
    float chroma = getChromaStrength();

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec4 result = vec4(0.0);

    vec3 borderClr = colorWithFallback(borderColor.rgb, vec3(0.5, 0.6, 1.0));

    // Vitality system: highlighted = vivid/energetic, dormant = desaturated/dim
    float vitality = isHighlighted ? 1.0 : 0.3;

    // ── Bass = Chromatic EXPLOSION ────────────────────────────────────
    // Bass hits spike chromatic aberration to 3-4x, then rapid decay (~0.3s).
    // Smoothed bass envelope using a fast-cycling sine that peaks on hits.
    float audioReact = getAudioReact();
    float bassChromaMul = getBassChromaMul();
    float bassEnvelope = hasAudio ? bass * bass : 0.0; // squared for punch
    float chromaSpike = 1.0 + bassEnvelope * bassChromaMul * audioReact; // 1x normal → 4x on full bass
    float chromaMod = chroma * chromaSpike;

    if (d < 0.0) {
        vec4 nexus = sampleNexus(fragCoord, localUV, isHighlighted ? chromaMod * 1.5 : chromaMod);
        vec3 zoneFill = fillColor.rgb;
        float tintAmount = getZoneFillTint();
        result.rgb = mix(nexus.rgb, nexus.rgb * zoneFill, tintAmount);
        result.a = fillOpacity;

        // Vitality-driven saturation and brightness
        float saturation = mix(0.5, 1.5, vitality);
        float brightness = mix(0.7, 1.3, vitality);
        float lum = luminance(result.rgb);
        result.rgb = mix(vec3(lum), result.rgb, saturation);
        result.rgb *= brightness;

        if (isHighlighted) {
            result.a = min(result.a + 0.06, 1.0);
        }

        // Inner glow: radial pulsing, vitality-driven
        float pulseSpeed = mix(0.5, 3.5, vitality);
        float pulseBase = 0.5 + 0.5 * sin(iTime * pulseSpeed);

        float glowStrength = mix(0.05, 0.3, vitality);
        float radialDist = length(localUV - 0.5) * 2.0;
        float innerGlow = max(0.0, (1.0 - radialDist)) * pulseBase * glowStrength;

        // Bass expands the glow outward with the chromatic punch
        float glowExpand = hasAudio ? bassEnvelope * 0.4 : 0.0;
        innerGlow *= (1.0 + glowExpand);

        // ── Mids = Glow Color Cycling ────────────────────────────────
        // Mids drive visible color cycling in the inner glow:
        // borderClr → fillColor → complementary color, looping with mids intensity.
        // The cycle phase advances faster with higher mids, creating a breathing effect.
        vec3 complementary = vec3(1.0) - borderClr; // complementary of border color
        float cyclePhase = hasAudio ? fract(iTime * (0.3 + mids * 2.0 * audioReact)) : 0.0;
        vec3 glowClr;
        if (cyclePhase < 0.333) {
            // borderClr → fillColor
            glowClr = mix(borderClr, fillColor.rgb, cyclePhase * 3.0);
        } else if (cyclePhase < 0.666) {
            // fillColor → complementary
            glowClr = mix(fillColor.rgb, complementary, (cyclePhase - 0.333) * 3.0);
        } else {
            // complementary → borderClr
            glowClr = mix(complementary, borderClr, (cyclePhase - 0.666) * 3.0);
        }
        // Blend between static borderClr glow and cycling glow based on mids
        float cycleBlend = hasAudio ? smoothstep(0.05, 0.5, mids) : 0.0;
        vec3 finalGlowClr = mix(borderClr, glowClr, cycleBlend);

        result.rgb += finalGlowClr * innerGlow;

        result.rgb = clamp(result.rgb, 0.0, 1.0);
    }

    // Border with angular energy flow
    float angle = atan(p.y, p.x);
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        // Angular flow animation: brightness racing around perimeter
        float flowSpeed = mix(0.3, 2.5, vitality);
        float flowRange = mix(0.1, 0.4, vitality);
        float flow = angularNoise(angle, 8.0, -iTime * flowSpeed) * flowRange + (1.0 - flowRange * 0.5);

        // Audio pulses the flow intensity
        float borderEnergy = 1.0 + (hasAudio ? bass * 0.4 : 0.0);
        float borderAlpha = border * mix(0.85, 0.95, vitality);

        vec3 flowColor = borderClr * flow * borderEnergy;

        // Highlighted: accent trace along border
        if (isHighlighted) {
            float accentTrace = angularNoise(angle, 6.0, iTime * 2.5);
            flowColor = mix(flowColor, fillColor.rgb * borderEnergy, accentTrace * 0.3);
        }

        result.rgb = mix(result.rgb, flowColor, borderAlpha);
        result.a = max(result.a, border * 0.98);
    }

    // Outer glow
    if (d > 0.0 && d < 22.0) {
        float glowRadius = mix(5.0, 9.0, vitality);
        float glowFalloff = mix(0.3, 0.6, vitality);
        // Bass expands outer glow (tied to eruption energy)
        if (hasAudio) {
            glowRadius += bassEnvelope * 6.0;
            glowFalloff += bass * 0.2;
        }
        float glow = expGlow(d, glowRadius, glowFalloff);

        // Dormant glow is much dimmer
        glow *= mix(0.3, 1.0, vitality);

        result.rgb += borderClr * glow;
        result.a = max(result.a, glow * 0.65);
    }

    // ── Treble = Edge Brightness ──────────────────────────────────
    // High treble subtly brightens the inner edge of the zone.
    if (hasAudio && treble > 0.06 && d > -borderWidth * 3.0 && d < 0.0) {
        float edgeProx = smoothstep(-borderWidth * 3.0, 0.0, d);
        result.rgb += borderClr * edgeProx * treble * 0.5;
    }

    return result;
}

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
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0)
            continue;
        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderNexusZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted,
            bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);
    fragColor = clampFragColor(color);
}
