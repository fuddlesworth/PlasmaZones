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
 *   [2].z = gridIntensity    — grid / mesh overlay brightness (0–0.5)
 *   [2].w = fillOpacity      — zone interior fill opacity (0–1)
 *
 * Colors:
 *   customColors[0] = primary neon (default: cyan)
 *   customColors[1] = accent glow (default: magenta)
 *   customColors[2] = bass color (default: orange)
 *   customColors[3] = spark color (default: white)
 */

// ─── Quintic C2 noise (eliminates visible lattice artifacts) ─────

vec2 quintic(vec2 f) {
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

float quintic1(float f) {
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

float qnoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = quintic(f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float qnoise1D(float x) {
    float i = floor(x);
    float f = fract(x);
    f = quintic1(f);
    return mix(hash11(i), hash11(i + 1.0), f);
}

// ─── FBM with per-octave rotation (5 octaves for rich detail) ────

float fbmField(vec2 uv, int octaves) {
    float value = 0.0;
    float amp = 0.5;
    float c = cos(0.5);
    float s = sin(0.5);
    mat2 rot = mat2(c, -s, s, c);
    for (int i = 0; i < octaves && i < 8; i++) {
        value += amp * qnoise2D(uv);
        uv = rot * uv * 2.0 + vec2(100.0);
        amp *= 0.55;
    }
    return value;
}


// ─── Per-zone rendering ──────────────────────────────────────────

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
    float gridRes       = customParams[3].w >= 0.0 ? customParams[3].w : 20.0;

    // Zone geometry
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

    // Colors
    vec3 primary  = colorWithFallback(customColors[0].rgb, fillColor.rgb);
    primary       = colorWithFallback(primary, vec3(0.0, 1.0, 1.0));
    vec3 accent   = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.0, 1.0));
    vec3 bassCol  = colorWithFallback(customColors[2].rgb, vec3(1.0, 0.4, 0.0));
    vec3 sparkCol = colorWithFallback(customColors[3].rgb, vec3(1.0, 1.0, 1.0));

    float vitality = zoneVitality(isHighlighted);
    primary = vitalityDesaturate(primary, vitality);
    accent  = vitalityDesaturate(accent, vitality);
    glowIntensity *= vitalityScale(0.5, 1.5, vitality);
    reactivity *= vitalityScale(0.6, 1.3, vitality);
    flowSpeed *= vitalityScale(0.6, 1.0, vitality);
    plasmaDetail *= vitalityScale(0.4, 1.0, vitality);

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
        float innerDist = -d;

        // Reactive fill with spatial structure from FBM
        float t = iTime * flowSpeed * 0.3;
        float fillNoise = fbmField(globalUV * 4.0 + t * 0.5, 4);
        float fillBreath = 0.06 + 0.06 * (energy + idlePulse) + fillNoise * 0.04;
        result.rgb = activeColor * fillBreath * intensity;
        result.a   = fillOpacity;

        // Inner glow: exponential falloff from border edge
        float innerGlow = exp(-innerDist / 30.0) * 0.3 * intensity;
        // Slight color variation on inner glow
        vec3 glowTint = mix(activeColor, accent, innerGlow * 0.5);
        result.rgb += glowTint * innerGlow;

        // ── Spectrum aurora (flowing noise field) ─────────────
        if (hasAudio && waveHeight > 0.02) {
            float fieldIntensity = waveHeight * 3.0;

            // Domain-warped UV for organic motion
            float bassWarpBoost = 1.0 + smoothstep(0.1, 0.4, bassHit) * 0.8;
            vec2 warpUV = globalUV;
            warpUV.x += qnoise2D(globalUV * 3.0 + vec2(t * 0.7, 0.0)) * 0.15 * bassWarpBoost;
            warpUV.y += qnoise2D(globalUV * 3.0 + vec2(0.0, t * 0.5)) * 0.15 * bassWarpBoost;

            // 5-octave FBM with rotation for richer detail
            float n = fbmField(warpUV * 4.0 + t * 0.4, 5);

            // Audio-reactive brightness: map spectrum to screen x position
            float specVal = audioBarSmooth(globalUV.x);
            float audioBright = 0.3 + 0.7 * specVal;

            // Color: primary (bass/left) to accent (treble/right)
            // with noise-driven variation for depth
            vec3 fieldColor = mix(primary, accent, globalUV.x);
            vec3 fieldColor2 = mix(accent, primary, n);
            fieldColor = mix(fieldColor, fieldColor2, 0.35);

            // Bass pulses tint the field toward bassColor
            if (bass > 0.15) {
                float bassTint = smoothstep(0.15, 0.5, bass) * 0.25;
                fieldColor = mix(fieldColor, bassCol, bassTint * (1.0 - globalUV.x));
            }

            float fieldBright = n * audioBright * fieldIntensity * reactivity;
            result.rgb += fieldColor * fieldBright * 0.5;
            result.a = max(result.a, 0.85 + 0.05 * fieldBright);
        }

        // ── Energy veins / tendrils ───────────────────────────
        if (veinIntensity > 0.01) {
            float veinSum = 0.0;
            float vt = iTime * flowSpeed * 0.5;

            // Multiple vein layers with noise-displaced paths
            for (int v = 0; v < 5; v++) {
                float vf = float(v);
                float yOff = vf * 0.19 + 0.05;

                // Horizontal veins: noise-displaced curves (not straight lines)
                float veinNoise = qnoise1D((globalUV.x + yOff) * 8.0 + vt + vf * 3.7);
                float veinY = yOff + veinNoise * 0.15;
                // Secondary displacement for curvature
                veinY += qnoise2D(vec2(globalUV.x * 12.0 + vf * 5.0, vt * 0.3)) * 0.04;
                float veinDist = abs(globalUV.y - veinY);
                float veinWidth = 0.005 + 0.003 * sin(vt * 2.0 + vf);
                float vein = smoothstep(veinWidth, 0.0, veinDist);

                // Vertical branching veins (also curved)
                float branchNoise = qnoise1D((globalUV.y + vf * 1.3) * 10.0 - vt * 0.8);
                float branchX = vf * 0.22 + 0.08 + branchNoise * 0.12;
                branchX += qnoise2D(vec2(vf * 7.0, globalUV.y * 10.0 + vt * 0.2)) * 0.03;
                float branchDist = abs(globalUV.x - branchX);
                float branch = smoothstep(0.004, 0.0, branchDist);

                // Junction glow where veins cross
                float junction = vein * branch;

                veinSum += vein + branch * 0.5 + junction * 2.0;
            }

            // Pulse with mids and overall energy (or idle pulse)
            float veinPulse = hasAudio
                ? 0.3 + 0.7 * (mids * 0.6 + overall * 0.4)
                : 0.4 + idlePulse * 0.4;
            vec3 veinColor = mix(primary, accent, 0.4 + 0.2 * sin(iTime * 1.5));
            result.rgb += veinColor * veinSum * veinPulse * veinIntensity * 0.4;
        }

        // ── Subtle grid / mesh for depth (screen-space) ──────
        if (gridOpacity > 0.001) {
            vec2 gridUV = globalUV * vec2(gridRes, gridRes * 0.7);
            float gt = iTime * 0.2;
            // Gentle warp preserves grid regularity
            gridUV += vec2(
                qnoise2D(globalUV * 3.0 + gt) * 0.06,
                qnoise2D(globalUV * 3.0 + gt + 50.0) * 0.06
            );

            vec2 gridLines = abs(fract(gridUV) - 0.5);
            float gridLine = min(gridLines.x, gridLines.y);
            float grid = 1.0 - smoothstep(0.0, 0.04, gridLine);

            float gridBright = (0.03 + 0.02 * energy) * (gridOpacity / 0.15);
            result.rgb += activeColor * grid * gridBright;
        }

        // ── Idle interior shimmer ─────────────────────────────
        if (!hasAudio && idleAnim > 0.01) {
            float shimmer = qnoise2D(globalUV * 6.0 + iTime * 0.3) * 0.04 * idleAnim;
            result.rgb += primary * shimmer;
        }
    }

    // ── Border (neon core with energy flow) ────────────────────

    float coreWidth = borderWidth * 0.7;
    float core = softBorder(d, coreWidth);
    if (core > 0.0) {
        // Seamless angular noise (no atan discontinuity)
        float borderAngle = atan(p.x, -p.y);

        float flow1 = angularNoise(borderAngle, 6.0, -iTime * flowSpeed + energy * 4.0);
        float flow2 = angularNoise(borderAngle, 3.0, iTime * flowSpeed * 0.7);
        float flowPulse = 0.5 + 0.3 * flow1 + 0.2 * flow2;

        // Plasma turbulence layered on top
        float plasma = qnoise2D(vec2(borderAngle * 3.2, iTime * 1.5) + p / max(length(rectSize), 1.0) * 2.0);
        plasma = plasma * plasmaDetail * 0.3;

        // Traveling energy pulse around perimeter
        float travelPhase = borderAngle / TAU + iTime * flowSpeed * 0.4;
        float travelPulse = pow(0.5 + 0.5 * sin(travelPhase * TAU * 3.0), 4.0);

        float borderBright = intensity * (flowPulse + plasma + travelPulse * 0.25);
        vec3 coreColor = activeColor * borderBright;

        // White-hot neon center (toned down so user color shows through)
        coreColor = mix(coreColor, vec3(1.0), core * 0.35);

        // Beat flash: border surges toward bassColor on strong bass
        if (bassHit > 0.5) {
            float flash = (bassHit - 0.5) * 1.5;
            coreColor = mix(coreColor, bassCol * 2.0, flash * core * 0.25);
        }

        result.rgb = max(result.rgb, coreColor * core);
        result.a   = max(result.a, core);
    }

    // ── Outer glow (bass-reactive expansion) ──────────────────

    float glowRadius = 20.0 + 5.0 * bassHit + 8.0 * idlePulse;
    if (d > 0.0 && d < glowRadius) {
        float glow1 = expGlow(d, glowRadius * 0.2, intensity * 0.35);
        float glow2 = expGlow(d, glowRadius * 0.5, intensity * 0.12);

        // Flowing color using seamless angular noise
        float glowAngle = atan(p.x, -p.y);
        float glowFlow = angularNoise(glowAngle, 4.0, -iTime * flowSpeed * 0.5);
        vec3 glowColor = mix(activeColor, accent, glowFlow * 0.3);

        result.rgb += glowColor * (glow1 + glow2);
        result.a    = max(result.a, (glow1 + glow2) * 0.5);
    }

    // ── Edge sparks (treble-driven, per-spark staggered timing) ─

    if (hasAudio && treble > 0.1 && abs(d) < borderWidth * 4.0) {
        float sparkAngle = atan(p.x, -p.y) / TAU + 0.5;
        float sparkSlot = floor(sparkAngle * 60.0);

        // Per-spark phase offset (staggered, not globally synchronous)
        float sparkPhase = hash21(vec2(sparkSlot, 0.0));
        float sparkRate = 15.0 + sparkPhase * 25.0; // 15–40 Hz per spark
        float sparkTime = iTime * sparkRate;
        float sparkRand = hash21(vec2(floor(sparkTime), sparkSlot));
        float sparkLife = fract(sparkTime);

        float spark = step(0.92, sparkRand) * (1.0 - sparkLife * sparkLife) * treble * 4.0;
        result.rgb += sparkCol * spark;
    }

    return result;
}

// ─── Custom Label Composite ───────────────────────────────────────

vec4 compositeNeonLabels(vec4 color, vec2 fragCoord,
                         float bass, float treble, float overall, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    float labelGlowSpread = customParams[3].x >= 0.0 ? customParams[3].x : 1.5;
    float labelBrightness = customParams[3].y >= 0.0 ? customParams[3].y : 2.0;
    float labelAudioReact = customParams[3].z >= 0.0 ? customParams[3].z : 1.0;

    vec3 primary  = colorWithFallback(customColors[0].rgb, vec3(0.0, 1.0, 1.0));
    vec3 accent   = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.0, 1.0));
    vec3 bassCol  = colorWithFallback(customColors[2].rgb, vec3(1.0, 0.4, 0.0));
    vec3 sparkCol = colorWithFallback(customColors[3].rgb, vec3(1.0, 1.0, 1.0));

    // Dual-layer Gaussian halo: inner (tight) and outer (wide)
    float innerHalo = 0.0;
    float outerHalo = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.3);
            vec2 off = vec2(float(dx), float(dy));
            innerHalo += texture(uZoneLabels, uv + off * px * labelGlowSpread).a * w;
            outerHalo += texture(uZoneLabels, uv + off * px * labelGlowSpread * 2.6).a * w;
        }
    }
    innerHalo /= 16.5;
    outerHalo /= 16.5;

    float innerOutline = innerHalo * (1.0 - labels.a);
    float outerOutline = outerHalo * (1.0 - innerHalo);

    // Outer glow: accent
    if (outerOutline > 0.01) {
        float pulse = hasAudio ? 0.6 + overall * 0.4 * labelAudioReact : 0.6;
        color.rgb += accent * outerOutline * 0.5 * pulse;
        color.a = max(color.a, outerOutline * 0.4);
    }

    // Inner glow: primary neon with treble sparks
    if (innerOutline > 0.01) {
        float pulse = hasAudio ? 0.8 + bass * 0.6 * labelAudioReact : 0.8;
        color.rgb += primary * innerOutline * 0.7 * pulse;

        // Treble sparks along the inner outline (use sparkColor)
        if (hasAudio && treble > 0.1) {
            float sparkSeed = hash11(floor(uv.x * iResolution.x * 0.5) +
                                     floor(uv.y * iResolution.y * 0.5) * 137.0);
            float sparkLife = fract(iTime * 12.0 + sparkSeed * 7.0);
            float spark = step(0.9, sparkSeed) * (1.0 - sparkLife) * treble * labelAudioReact * 3.0;
            color.rgb += sparkCol * innerOutline * spark;
        }
        color.a = max(color.a, innerOutline * 0.6);
    }

    // Core: flowing energy through the label
    if (labels.a > 0.01) {
        float flow = sin(uv.x * 40.0 - iTime * 3.0) * 0.5 + 0.5;
        vec3 core = color.rgb * labelBrightness + mix(primary, accent, flow) * 0.4;
        float energyPulse = hasAudio ? 1.0 + overall * 0.8 * labelAudioReact : 1.0;
        core *= energyPulse;
        color.rgb = mix(color.rgb, core, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}

// ─── Main ─────────────────────────────────────────────────────────

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    // Audio analysis (soft variants for noise-floor gating)
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

    color = compositeNeonLabels(color, fragCoord, bass, treble, overall, hasAudio);
    fragColor = clampFragColor(color);
}
