// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

/*
 * SPECTRUM PULSE — Reactive Neon Energy
 *
 * Neon borders surge with audio energy. Bass thumps expand the glow,
 * energy pulses race around the perimeter with the beat, spectrum data
 * paints a flowing aurora, treble fires off edge sparks.
 *
 * Parameters (customParams):
 *   [0].x = glowIntensity    — border glow brightness (1–5)
 *   [0].y = reactivity       — audio sensitivity (0.5–3)
 *   [0].z = waveHeight       — spectrum aurora height (0.05–0.4)
 *   [0].w = bassExpand       — bass glow expansion (0–3)
 *   [1].x = flowSpeed        — border energy flow speed (0.5–4)
 *   [1].y = plasmaDetail     — edge plasma turbulence (0–2)
 *   [1].z = colorMix         — audio-driven primary↔accent shift (0–1)
 *   [1].w = idleAnimation    — animation when silent (0–2)
 *
 * Colors:
 *   customColors[0] = primary neon (default: cyan)
 *   customColors[1] = accent glow (default: magenta)
 */

// ─── Noise ────────────────────────────────────────────────────────

float noise1D(float x) {
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(hash11(i), hash11(i + 1.0), f);
}

float noise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// ─── Frequency bands ──────────────────────────────────────────────

float getBass() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    int n = min(iAudioSpectrumSize, 8);
    for (int i = 0; i < n; i++) sum += audioBar(i);
    return sum / float(n);
}

float getMids() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    int lo = iAudioSpectrumSize / 4;
    int hi = iAudioSpectrumSize * 3 / 4;
    for (int i = lo; i < hi && i < iAudioSpectrumSize; i++) sum += audioBar(i);
    return sum / float(max(hi - lo, 1));
}

float getTreble() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    int lo = iAudioSpectrumSize * 3 / 4;
    for (int i = lo; i < iAudioSpectrumSize; i++) sum += audioBar(i);
    return sum / float(max(iAudioSpectrumSize - lo, 1));
}

float getOverall() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    for (int i = 0; i < iAudioSpectrumSize; i++) sum += audioBar(i);
    return sum / float(iAudioSpectrumSize);
}

// ─── Per-zone rendering ───────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    // Parameters with defaults
    float glowIntensity = customParams[0].x > 0.1   ? customParams[0].x : 2.5;
    float reactivity    = customParams[0].y > 0.1   ? customParams[0].y : 1.5;
    float waveHeight    = customParams[0].z > 0.01  ? customParams[0].z : 0.15;
    float bassExpand    = customParams[0].w > 0.001  ? customParams[0].w : 1.5;
    float flowSpeed     = customParams[1].x > 0.1   ? customParams[1].x : 2.0;
    float plasmaDetail  = customParams[1].y > 0.001  ? customParams[1].y : 0.8;
    float colorMix      = customParams[1].z > 0.001  ? customParams[1].z : 0.5;
    float idleAnim      = customParams[1].w > 0.01  ? customParams[1].w : 1.0;

    // Zone geometry
    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Colors
    vec3 primary = colorWithFallback(customColors[0].rgb, fillColor.rgb);
    primary      = colorWithFallback(primary, vec3(0.0, 1.0, 1.0));
    vec3 accent  = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.0, 1.0));
    if (isHighlighted) {
        primary = accent;
        glowIntensity *= 1.3;
    }

    // Audio analysis
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass     = getBass();
    float mids     = getMids();
    float treble   = getTreble();
    float overall  = getOverall();

    // Derived modifiers
    float energy     = hasAudio ? overall * reactivity : 0.0;
    float bassHit    = hasAudio ? bass * bassExpand : 0.0;
    float idlePulse  = hasAudio ? 0.0 : (0.5 + 0.5 * sin(iTime * 1.2 * PI)) * idleAnim;
    float intensity  = glowIntensity * (1.0 + energy + idlePulse * 0.3);

    // Color: blend primary → accent based on treble-to-bass ratio
    float colorT     = hasAudio ? clamp(treble / max(bass + 0.01, 0.1) * colorMix, 0.0, 1.0) : 0.0;
    vec3 activeColor = mix(primary, accent, colorT);

    // Perimeter angle for flow effects (0-1 clockwise from top-center)
    float angle = atan(p.x, -p.y) / TAU + 0.5;

    vec4 result = vec4(0.0);

    // ── Zone interior ─────────────────────────────────────────

    if (d < 0.0) {
        // Subtle reactive fill
        float fillBreath = 0.04 + 0.03 * (energy + idlePulse);
        result.rgb = activeColor * fillBreath * intensity;
        result.a   = 0.82;

        // Inner glow: brighter near the border edge
        float innerDist = -d;
        float innerGlow = exp(-innerDist / 30.0) * 0.3 * intensity;
        result.rgb += activeColor * innerGlow;

        // ── Spectrum aurora wave ──────────────────────────────
        if (hasAudio && waveHeight > 0.02) {
            float waveTop = 1.0 - waveHeight;
            if (localUV.y > waveTop) {
                float inWave = (localUV.y - waveTop) / waveHeight; // 0 at top of wave, 1 at bottom
                float specVal = audioBarSmooth(localUV.x);

                // The bar fills upward from the bottom of the zone:
                // filled when (1.0 - inWave) < specVal
                float barPos = 1.0 - inWave;
                if (barPos < specVal) {
                    // Gradient color: low frequency (left) = primary, high (right) = accent
                    vec3 specColor = mix(primary, accent, localUV.x);
                    float bright = 0.4 + 0.6 * specVal;

                    // Soft fade at the bar's upper edge
                    float edgeFade = smoothstep(specVal, specVal - 0.08, barPos);
                    result.rgb += specColor * bright * edgeFade * reactivity;
                    result.a = max(result.a, 0.88);
                }

                // Glow above the wave peaks
                float peakDist = barPos - specVal;
                if (peakDist > 0.0 && peakDist < 0.15) {
                    vec3 specColor = mix(primary, accent, localUV.x);
                    float aurGlow = exp(-peakDist / 0.05) * specVal * 0.4 * reactivity;
                    result.rgb += specColor * aurGlow;
                }
            }
        }

        // ── Bass center pulse ─────────────────────────────────
        if (bassHit > 0.3) {
            float centerDist = length(localUV - 0.5) * 2.0;
            float pulse = (1.0 - smoothstep(0.0, 1.0, centerDist)) * (bassHit - 0.3) * 0.12;
            result.rgb += activeColor * pulse;
        }

        // ── Idle interior shimmer (when silent) ───────────────
        if (!hasAudio && idleAnim > 0.01) {
            float shimmer = noise2D(localUV * 6.0 + iTime * 0.3) * 0.04 * idleAnim;
            result.rgb += primary * shimmer;
        }
    }

    // ── Border (neon core with energy flow) ───────────────────

    float coreWidth = borderWidth * 0.7;
    float core = softBorder(d, coreWidth);
    if (core > 0.0) {
        // Energy flow: animated brightness racing around perimeter
        float flow1 = noise1D(angle * 12.0 - iTime * flowSpeed + energy * 4.0);
        float flow2 = noise1D(angle * 6.0  + iTime * flowSpeed * 0.7);
        float flowPulse = 0.5 + 0.3 * flow1 + 0.2 * flow2;

        // Plasma turbulence layered on top
        float plasma = noise2D(vec2(angle * 20.0, iTime * 1.5) + p * 0.008);
        plasma = plasma * plasmaDetail * 0.3;

        float borderBright = intensity * (flowPulse + plasma);
        vec3 coreColor = activeColor * borderBright;

        // White-hot center of the neon tube
        coreColor = mix(coreColor, vec3(1.0), core * 0.6);

        // Beat flash: border goes white on strong bass
        if (bassHit > 0.5) {
            float flash = (bassHit - 0.5) * 1.5;
            coreColor = mix(coreColor, vec3(1.0), flash * core * 0.3);
        }

        result.rgb = max(result.rgb, coreColor * core);
        result.a   = max(result.a, core);
    }

    // ── Outer glow (bass-reactive expansion) ──────────────────

    float glowRadius = 20.0 + 30.0 * bassHit + 8.0 * idlePulse;
    if (d > 0.0 && d < glowRadius) {
        // Dual-layer glow: sharp inner + soft outer
        float glow1 = expGlow(d, glowRadius * 0.2, intensity * 0.35);
        float glow2 = expGlow(d, glowRadius * 0.5, intensity * 0.12);

        // Flowing color in the glow halo
        float glowFlow = noise1D(angle * 8.0 - iTime * flowSpeed * 0.5);
        vec3 glowColor = mix(activeColor, accent, glowFlow * 0.3);

        result.rgb += glowColor * (glow1 + glow2);
        result.a    = max(result.a, (glow1 + glow2) * 0.5);
    }

    // ── Edge sparks (treble-driven) ───────────────────────────

    if (hasAudio && treble > 0.1 && abs(d) < borderWidth * 4.0) {
        // Each spark is a short-lived bright point on the border
        float sparkTime = floor(iTime * 40.0);
        float sparkSlot = floor(angle * 60.0);
        float sparkRand = hash21(vec2(sparkTime, sparkSlot));
        float sparkLife = fract(iTime * 40.0);

        // Only ~8% of slots spark; intensity fades over lifetime
        float spark = step(0.92, sparkRand) * (1.0 - sparkLife * sparkLife) * treble * 4.0;
        result.rgb += vec3(1.0) * spark;
    }

    return result;
}

// ─── Main ─────────────────────────────────────────────────────────

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);

        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);
    fragColor = clampFragColor(color);
}
