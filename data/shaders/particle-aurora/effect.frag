// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>
#include <particles.glsl>

/*
 * PARTICLE AURORA — Fragment Shader (Image Pass)
 *
 * Composites compute-generated particle texture over zone rendering.
 * Adds zone borders, labels, glow, and atmospheric background.
 */

void main() {
    vec2 fc = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    float glowIntensity = max(customParams[1].y, 0.5);
    vec3 primary = colorWithFallback(customColors[0].rgb, vec3(0.04, 0.0, 0.13));
    vec3 aurora1 = colorWithFallback(customColors[1].rgb, vec3(0.0, 1.0, 0.53));
    vec3 aurora2 = colorWithFallback(customColors[2].rgb, vec3(0.0, 0.53, 1.0));

    bool hasAudio = iAudioSpectrumSize > 0;
    float bass = hasAudio ? getBass() : 0.0;
    float energy = hasAudio ? getOverall() * customParams[1].z : 0.0;

    // Sample particle texture (screen-space)
    vec2 particleUv = fc / max(iResolution, vec2(1.0));
    vec4 particleSample = texture(uParticleTexture, particleUv);

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 fillCol = zoneFillColors[i];
        vec4 borderCol = zoneBorderColors[i];
        vec4 params = zoneParams[i];
        bool isHighlighted = params.z > 0.5;
        float vitality = zoneVitality(isHighlighted);

        float borderRadius = max(params.x, 6.0);
        float borderWidth = max(params.y, 2.5);

        vec2 rectPos = zoneRectPos(rect);
        vec2 rectSize = zoneRectSize(rect);
        vec2 center = rectPos + rectSize * 0.5;
        vec2 p = fc - center;
        float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

        vec4 result = vec4(0.0);

        if (d < 0.0) {
            // Background: dark primary with subtle gradient
            vec2 localUv = (fc - rectPos) / max(rectSize, vec2(1.0));
            vec3 bg = primary;
            bg += primary * 0.3 * (1.0 - length(localUv - 0.5));

            // Subtle aurora shimmer in background
            float shimmer = sin(localUv.x * 8.0 + iTime * 0.5) * sin(localUv.y * 6.0 + iTime * 0.3);
            bg += mix(aurora1, aurora2, localUv.x) * 0.02 * (0.5 + 0.5 * shimmer);

            // Composite particles over background
            vec3 col = bg + particleSample.rgb;

            // Audio energy boost
            col *= 1.0 + energy * 0.15;

            // Vitality desaturation for non-highlighted zones
            if (!isHighlighted) {
                col = vitalityDesaturate(col, 0.3);
            }

            result.rgb = col;
            result.a = 0.9;

            // Inner edge glow
            float innerGlow = exp(d / mix(30.0, 14.0, vitality)) * mix(0.03, 0.1, vitality);
            innerGlow *= glowIntensity * (0.4 + energy * 0.3);
            result.rgb += aurora1 * innerGlow;

            // Labels
            if (customParams[2].x > 0.5) {
                vec2 labelUv = fc / max(iResolution, vec2(0.001));
                vec4 labelSample = texture(uZoneLabels, labelUv);
                if (labelSample.a > 0.01) {
                    vec3 labelCol = aurora1 * 0.8 + vec3(0.2);
                    result.rgb = mix(result.rgb, labelCol, labelSample.a);
                    result.a = max(result.a, labelSample.a);
                }
            }
        }

        // Border
        float coreWidth = borderWidth * mix(0.5, 0.9, vitality);
        float core = softBorder(d, coreWidth);
        if (core > 0.0) {
            float borderAngle = atan(p.x, -p.y) / TAU + 0.5;
            float borderEnergy = 1.0 + energy * mix(0.2, 0.8, vitality);
            vec3 coreColor = mix(aurora1, aurora2, borderAngle) * glowIntensity * borderEnergy;

            float flow = angularNoise(borderAngle, 12.0, -iTime * mix(0.3, 1.5, vitality));
            coreColor *= mix(0.6, 1.2, flow);

            if (hasAudio && bass > 0.5) {
                float flash = (bass - 0.5) * 2.0 * vitality;
                coreColor = mix(coreColor, aurora2 * 2.0, flash * core * 0.3);
            }

            result.rgb = max(result.rgb, coreColor * core);
            result.a = max(result.a, core);
        }

        // Outer glow
        float baseGlowR = mix(6.0, 16.0, vitality);
        float glowRadius = baseGlowR + energy * 4.0;
        if (d > 0.0 && d < glowRadius) {
            float glow1 = expGlow(d, glowRadius * 0.2, glowIntensity * mix(0.08, 0.25, vitality));
            float glow2 = expGlow(d, glowRadius * 0.5, glowIntensity * mix(0.03, 0.08, vitality));
            vec3 glowColor = mix(aurora1, aurora2, 0.5) * 0.5;
            result.rgb += glowColor * (glow1 + glow2);
            result.a = max(result.a, (glow1 + glow2) * 0.4);
        }

        color = blendOver(color, result);
    }

    // Vignette
    vec2 q = fc / max(iResolution, vec2(1.0));
    float vig = 0.5 + 0.5 * pow(16.0 * q.x * q.y * (1.0 - q.x) * (1.0 - q.y), 0.1);
    color.rgb *= vig;

    fragColor = clampFragColor(color);
}
