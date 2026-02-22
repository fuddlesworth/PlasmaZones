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
 *   [2].x = veinIntensity    — energy vein / tendril brightness (0–1)
 *   [2].y = radialWaves      — bass radial wave strength (0–1)
 *   [2].z = gridIntensity    — grid / mesh overlay brightness (0–0.15)
 *   [2].w = fillOpacity      — zone interior fill opacity (0–1)
 *
 * Colors:
 *   customColors[0] = primary neon (default: cyan)
 *   customColors[1] = accent glow (default: magenta)
 */



// ─── Per-zone rendering (full-screen effect with zone cutout) ─────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted,
                float bass, float mids, float treble, float overall, bool hasAudio)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    // Parameters with defaults (sentinel: -1.0 = unset → use default)
    float glowIntensity = customParams[0].x >= 0.0 ? customParams[0].x : 2.5;
    float reactivity    = customParams[0].y >= 0.0 ? customParams[0].y : 1.5;
    float waveHeight    = customParams[0].z >= 0.0 ? customParams[0].z : 0.15;
    float bassExpand    = customParams[0].w >= 0.0 ? customParams[0].w : 1.5;
    float flowSpeed     = customParams[1].x >= 0.0 ? customParams[1].x : 2.0;
    float plasmaDetail  = customParams[1].y >= 0.0 ? customParams[1].y : 0.8;
    float colorMix      = customParams[1].z >= 0.0 ? customParams[1].z : 0.5;
    float idleAnim      = customParams[1].w >= 0.0 ? customParams[1].w : 1.0;
    float veinIntensity = customParams[2].x >= 0.0 ? customParams[2].x : 0.5;
    float radialWaveStr = customParams[2].y >= 0.0 ? customParams[2].y : 1.0;
    float gridOpacity   = customParams[2].z >= 0.0 ? customParams[2].z : 0.15;
    float fillOpacity   = customParams[2].w >= 0.0 ? customParams[2].w : 0.85;

    // Zone geometry (zone-local: used for SDF, border, glow)
    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Screen-space UV: continuous across zones (0-1 over entire screen)
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));

    // Screen-space angle (from screen center, for interior effects)
    vec2 screenCenter = iResolution.xy * 0.5;
    vec2 sp = fragCoord - screenCenter;
    float angle = atan(sp.x, -sp.y) / TAU + 0.5;

    // Zone-local angle (follows zone perimeter, for border/spark effects)
    float borderAngle = atan(p.x, -p.y) / TAU + 0.5;

    // Colors
    vec3 primary = colorWithFallback(customColors[0].rgb, fillColor.rgb);
    primary      = colorWithFallback(primary, vec3(0.0, 1.0, 1.0));
    vec3 accent  = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.0, 1.0));
    if (isHighlighted) {
        primary = accent;
        glowIntensity *= 1.3;
    }

    // Derived modifiers
    float energy     = hasAudio ? overall * reactivity : 0.0;
    float bassHit    = hasAudio ? bass * bassExpand : 0.0;
    float idlePulse  = hasAudio ? 0.0 : (0.5 + 0.5 * sin(iTime * 1.2 * PI)) * idleAnim;
    float intensity  = glowIntensity * (1.0 + energy + idlePulse * 0.3);

    // Color: blend primary → accent based on treble-to-bass ratio
    float colorT     = hasAudio ? clamp(treble / max(bass + 0.01, 0.1) * colorMix, 0.0, 1.0) : 0.0;
    vec3 activeColor = mix(primary, accent, colorT);

    vec4 result = vec4(0.0);

    // ── Zone interior ─────────────────────────────────────────

    if (d < 0.0) {
        // Subtle reactive fill
        float fillBreath = 0.04 + 0.03 * (energy + idlePulse);
        result.rgb = activeColor * fillBreath * intensity;
        result.a   = fillOpacity;

        // Inner glow: brighter near the border edge
        float innerDist = -d;
        float innerGlow = exp(-innerDist / 30.0) * 0.3 * intensity;
        result.rgb += activeColor * innerGlow;

        // ── Full-screen flowing noise field ───────────────────
        if (hasAudio && waveHeight > 0.02) {
            // Overall intensity from waveHeight parameter
            float fieldIntensity = waveHeight * 3.0;

            // Warped UV for organic motion (screen-space)
            float t = iTime * flowSpeed * 0.3;
            vec2 warpUV = globalUV;
            warpUV.x += noise2D(globalUV * 3.0 + vec2(t * 0.7, 0.0)) * 0.15;
            warpUV.y += noise2D(globalUV * 3.0 + vec2(0.0, t * 0.5)) * 0.15;

            // Multi-octave FBM noise field
            float n = 0.0;
            n += noise2D(warpUV * 4.0 + vec2(t * 0.4, t * 0.3)) * 0.5;
            n += noise2D(warpUV * 8.0 - vec2(t * 0.6, t * 0.2)) * 0.25;
            n += noise2D(warpUV * 16.0 + vec2(t * 0.8, -t * 0.5)) * 0.125;
            n = n / 0.875; // Normalize to 0-1

            // Audio-reactive brightness: map spectrum to screen x position
            float specVal = audioBarSmooth(globalUV.x);
            float audioBright = 0.3 + 0.7 * specVal;

            // Color gradient: primary (bass/left) to accent (treble/right)
            vec3 fieldColor = mix(primary, accent, globalUV.x);

            // Additional color variation from noise
            vec3 fieldColor2 = mix(accent, primary, n);
            fieldColor = mix(fieldColor, fieldColor2, 0.3);

            float fieldBright = n * audioBright * fieldIntensity * reactivity;
            result.rgb += fieldColor * fieldBright * 0.5;
            result.a = max(result.a, 0.85 + 0.05 * fieldBright);
        }

        // ── Energy veins / tendrils ───────────────────────────
        if (hasAudio) {
            float veinSum = 0.0;
            float t = iTime * flowSpeed * 0.5;

            // Multiple vein layers at different orientations (screen-space)
            for (int v = 0; v < 5; v++) {
                float vf = float(v);
                float yOff = vf * 0.19 + 0.05;
                // Horizontal veins: sample noise along x, offset by y
                float veinNoise = noise1D((globalUV.x + yOff) * 8.0 + t + vf * 3.7);
                // Vein is where noise crosses a threshold — thin line
                float veinY = yOff + veinNoise * 0.15;
                float veinDist = abs(globalUV.y - veinY);
                float veinWidth = 0.006 + 0.004 * sin(t * 2.0 + vf);
                float vein = smoothstep(veinWidth, 0.0, veinDist);

                // Vertical branching veins
                float branchNoise = noise1D((globalUV.y + vf * 1.3) * 10.0 - t * 0.8);
                float branchX = vf * 0.22 + 0.08 + branchNoise * 0.12;
                float branchDist = abs(globalUV.x - branchX);
                float branch = smoothstep(0.005, 0.0, branchDist);

                veinSum += vein + branch * 0.5;
            }

            // Pulse with mids and overall energy
            float veinPulse = 0.3 + 0.7 * (mids * 0.6 + overall * 0.4);
            vec3 veinColor = mix(primary, accent, 0.4 + 0.2 * sin(iTime * 1.5));
            result.rgb += veinColor * veinSum * veinPulse * veinIntensity * 0.4 * reactivity;
        }

        // ── Radial energy waves from screen center ────────────
        if (bassHit > 0.15) {
            float centerDist = length(globalUV - 0.5) * 2.0;
            float bassAmp = (bassHit - 0.15) * 1.2;

            // Multiple expanding concentric rings at different speeds
            float ring1 = sin((centerDist - iTime * 1.5) * 18.0) * 0.5 + 0.5;
            ring1 *= smoothstep(1.2, 0.0, centerDist); // Fade at edges
            float ring2 = sin((centerDist - iTime * 2.2) * 12.0) * 0.5 + 0.5;
            ring2 *= smoothstep(1.0, 0.0, centerDist);
            float ring3 = sin((centerDist - iTime * 0.8) * 24.0) * 0.5 + 0.5;
            ring3 *= smoothstep(1.4, 0.2, centerDist);

            // Sharpen rings to thin bands
            ring1 = pow(ring1, 4.0);
            ring2 = pow(ring2, 5.0);
            ring3 = pow(ring3, 6.0);

            float rings = (ring1 * 0.5 + ring2 * 0.3 + ring3 * 0.2);
            result.rgb += activeColor * rings * bassAmp * 0.35 * radialWaveStr;
        }

        // ── Subtle grid / mesh for depth (screen-space) ──────
        {
            // Grid coordinates with slight noise distortion
            vec2 gridUV = globalUV * vec2(20.0, 14.0);
            float t = iTime * 0.2;
            gridUV += vec2(
                noise2D(globalUV * 3.0 + t) * 0.4,
                noise2D(globalUV * 3.0 + t + 50.0) * 0.4
            );

            // Rectangular grid lines
            vec2 gridLines = abs(fract(gridUV) - 0.5);
            float gridLine = min(gridLines.x, gridLines.y);
            float grid = 1.0 - smoothstep(0.0, 0.04, gridLine);

            // Very subtle brightness
            float gridBright = (0.03 + 0.02 * energy) * (gridOpacity / 0.15);
            result.rgb += activeColor * grid * gridBright;
        }

        // ── Idle interior shimmer (when silent, screen-space) ─
        if (!hasAudio && idleAnim > 0.01) {
            float shimmer = noise2D(globalUV * 6.0 + iTime * 0.3) * 0.04 * idleAnim;
            result.rgb += primary * shimmer;
        }
    }

    // ── Border (neon core with energy flow, zone-local angle) ─

    float coreWidth = borderWidth * 0.7;
    float core = softBorder(d, coreWidth);
    if (core > 0.0) {
        // Energy flow: animated brightness racing around perimeter
        float flow1 = noise1D(borderAngle * 12.0 - iTime * flowSpeed + energy * 4.0);
        float flow2 = noise1D(borderAngle * 6.0  + iTime * flowSpeed * 0.7);
        float flowPulse = 0.5 + 0.3 * flow1 + 0.2 * flow2;

        // Plasma turbulence layered on top
        float plasma = noise2D(vec2(borderAngle * 20.0, iTime * 1.5) + p * 0.008);
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

    // ── Outer glow (bass-reactive expansion, zone-local angle) ─

    float glowRadius = 20.0 + 30.0 * bassHit + 8.0 * idlePulse;
    if (d > 0.0 && d < glowRadius) {
        // Dual-layer glow: sharp inner + soft outer
        float glow1 = expGlow(d, glowRadius * 0.2, intensity * 0.35);
        float glow2 = expGlow(d, glowRadius * 0.5, intensity * 0.12);

        // Flowing color in the glow halo
        float glowFlow = noise1D(borderAngle * 8.0 - iTime * flowSpeed * 0.5);
        vec3 glowColor = mix(activeColor, accent, glowFlow * 0.3);

        result.rgb += glowColor * (glow1 + glow2);
        result.a    = max(result.a, (glow1 + glow2) * 0.5);
    }

    // ── Edge sparks (treble-driven, zone-local angle) ─────────

    if (hasAudio && treble > 0.1 && abs(d) < borderWidth * 4.0) {
        // Each spark is a short-lived bright point on the border
        float sparkTime = floor(iTime * 40.0);
        float sparkSlot = floor(borderAngle * 60.0);
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

    // Audio analysis (computed once for all zones)
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBass();
    float mids    = getMids();
    float treble  = getTreble();
    float overall = getOverall();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);

        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);
    fragColor = clampFragColor(color);
}
