// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * COSMIC FLOW - Fragment Shader
 *
 * Renders a SINGLE continuous cosmic flow field across the entire overlay.
 * All pattern/animation uses fragCoord (screen space) so FBM flows seamlessly
 * across zone boundaries -- zones are windows into the shared field.
 * Based on Inigo Quilez's palette technique and classic fbm.
 *
 * Features highlight/dormant vitality system, secondary detail veins,
 * inner edge glow with bevel, and audio reactivity.
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// ── Noise helpers ───────────────────────────────────────────────

// Pseudo-random 2D hash
float rand2D(in vec2 p) {
    return fract(sin(dot(p, vec2(15.285, 97.258))) * 47582.122);
}

// Quintic interpolation for C2 continuity; eliminates visible cell boundaries
vec2 quintic(vec2 f) {
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

// Value noise
float noise(in vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    float a = rand2D(i);
    float b = rand2D(i + vec2(1.0, 0.0));
    float c = rand2D(i + vec2(0.0, 1.0));
    float d = rand2D(i + vec2(1.0, 1.0));

    vec2 u = quintic(f);
    float lower = mix(a, b, u.x);
    float upper = mix(c, d, u.x);

    return mix(lower, upper, u.y);
}

// Fractal Brownian Motion with rotation
float fbm(in vec2 uv, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;

    float c = cos(0.5);
    float s = sin(0.5);
    mat2 rot = mat2(c, -s, s, c);

    for (int i = 0; i < octaves && i < 8; i++) {
        value += amplitude * noise(uv);
        uv = rot * uv * 2.0 + vec2(180.0);
        amplitude *= 0.6;
    }

    return value;
}

// Inigo Quilez palette function
vec3 palette(in float t, in vec3 a, in vec3 b, in vec3 c, in vec3 d) {
    return a + b * cos(TAU * (c * t + d));
}

vec4 renderCosmicZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted,
                      float bass, float mids, float treble, float overall, bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);

    // Get shader parameters with defaults
    float speed = customParams[0].x >= 0.0 ? customParams[0].x : 0.1;
    float flowSpeed = customParams[0].y >= 0.0 ? customParams[0].y : 0.3;
    float noiseScale = customParams[0].z >= 0.0 ? customParams[0].z : 3.0;
    int octaves = int(customParams[0].w >= 0.0 ? customParams[0].w : 6.0);

    float colorShift = customParams[1].x >= 0.0 ? customParams[1].x : 0.0;
    float saturation = customParams[1].y >= 0.0 ? customParams[1].y : 0.5;
    float brightness = customParams[1].z >= 0.0 ? customParams[1].z : 0.5;
    float contrast = customParams[1].w >= 0.0 ? customParams[1].w : 0.95;

    float fillOpacity = customParams[2].x >= 0.0 ? customParams[2].x : 0.85;
    float borderGlow = customParams[2].y >= 0.0 ? customParams[2].y : 0.3;
    float edgeFadeStart = customParams[2].z < 0.0 ? customParams[2].z : -30.0;
    float borderBrightness = customParams[2].w >= 0.0 ? customParams[2].w : 1.3;

    float audioReact = customParams[3].x >= 0.0 ? customParams[3].x : 1.0;
    float veinDetail = customParams[3].y >= 0.0 ? customParams[3].y : 0.25;
    float innerGlowStr = customParams[3].z >= 0.0 ? customParams[3].z : 0.25;
    float sparkleStr = customParams[3].w >= 0.0 ? customParams[3].w : 2.5;

    // Convert rect to pixel coordinates
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;

    // Position relative to zone center
    vec2 p = fragCoord - center;

    // Calculate SDF
    float d = sdRoundedBox(p, halfSize, borderRadius);

    // Zone-local UV (still needed for inner edge glow calculations)
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);

    // Global screen-space UV for pattern generation -- one continuous field
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    vec2 centeredUV = (globalUV * 2.0 - 1.0) * noiseScale;

    // Aspect correction (screen-space)
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    centeredUV.x *= aspect;

    // Palette colors from uniforms or defaults
    vec3 palA = customColors[0].rgb;
    vec3 palB = customColors[1].rgb;
    vec3 palC = customColors[2].rgb;
    vec3 palD = customColors[3].rgb;

    // IQ palette: when palette colors are unset, palA/palB become gray (brightness/saturation);
    // set palette colors in the UI for full control.
    palA = colorWithFallback(palA, vec3(brightness));
    palB = colorWithFallback(palB, vec3(saturation));
    palC = colorWithFallback(palC, vec3(1.0));
    palD = colorWithFallback(palD, vec3(0.0, 0.10, 0.20));

    // Apply color shift to phase
    palD += vec3(colorShift);

    // ── Vitality system ─────────────────────────────────────
    float vitality = isHighlighted ? 1.0 : 0.3;

    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(iTime * 0.8 * PI)) * 0.5;

    // ── Identity-native audio: cosmic / nebula ──────────────

    // ** Cosmic Breath (overall energy) **
    // Overall energy modulates noise scale: high energy zooms in (larger features),
    // low energy zooms out (finer, denser patterns). Smooth and non-jittery.
    float breathAmount = hasAudio ? mix(0.85, 1.35, smoothstep(0.0, 0.6, overall)) : 1.0;
    centeredUV /= breathAmount; // dividing UV = zooming into noise
    noiseScale /= breathAmount; // keep scale consistent for downstream use

    // ** Color Temperature Shift (mids) **
    // Mids shift the entire nebula color temperature via palA (brightness)
    // and palB (saturation). Low mids = cool blue-shift, high mids = warm red-shift.
    if (hasAudio) {
        float tempShift = smoothstep(0.0, 0.5, mids * audioReact); // 0 = cool, 1 = warm
        // Cool bias: slightly bluer brightness, more blue saturation
        // Warm bias: slightly warmer brightness, more red saturation
        vec3 coolA = palA * vec3(0.85, 0.9, 1.15);
        vec3 warmA = palA * vec3(1.15, 0.95, 0.85);
        vec3 coolB = palB * vec3(0.8, 0.95, 1.2);
        vec3 warmB = palB * vec3(1.2, 0.95, 0.8);
        palA = mix(coolA, warmA, tempShift);
        palB = mix(coolB, warmB, tempShift);
    }

    // ** Gravity Wells / Shockwave Ripples (bass) **
    // Bass creates localized gravity-well distortions in UV space before FBM.
    // 3 semi-random seed points (seeded by slow time) emit radial ripples.
    if (hasAudio && bass > 0.02) {
        float bassDecay = smoothstep(0.0, 0.15, bass); // smooth envelope
        float slowSeed = floor(iTime * 0.7); // changes seed ~every 1.4s
        for (int gw = 0; gw < 3; gw++) {
            // Seed point in centered UV space, varies with time
            vec2 wellCenter = (hash22(vec2(float(gw) * 31.7, slowSeed * 17.3)) * 2.0 - 1.0)
                              * noiseScale * 0.6;
            wellCenter.x *= aspect;
            vec2 toFrag = centeredUV - wellCenter;
            float dist = length(toFrag);
            // Ripple: radial sine wave that propagates outward, gated by bass
            float ripple = sin(dist * 8.0 - iTime * 5.0) * exp(-dist * 1.5);
            // Warp UV radially from the well center
            centeredUV += normalize(toFrag + 0.0001) * ripple * bassDecay * 0.15 * audioReact;
        }
    }

    // Pattern speed is constant across all zones so the full-screen field is continuous.
    // Vitality only affects post-computation brightness/saturation, never the pattern itself.
    float vSpeed = speed;
    float vFlowSpeed = flowSpeed;

    vec4 result = vec4(0.0);
    float time = iTime;

    // Inside the zone
    if (d < 0.0) {

        // First fbm layer - slow drift
        float q = fbm(centeredUV + time * vSpeed, octaves);

        // Second fbm layer - combines with first for complexity
        float r = fbm(centeredUV + q + time * vFlowSpeed, octaves);

        // Generate color from palette
        vec3 col = palette(r * contrast, palA, palB, palC, palD);

        // ── Secondary detail layer: noise veins ─────────────
        // Thin vein lines at the 0.5 contour of a higher-frequency noise
        float veinNoise = noise(centeredUV * 6.0 + time * vSpeed * 0.5);
        float veins = 1.0 - smoothstep(0.0, 0.06, abs(veinNoise - 0.5));
        // Color veins slightly offset from base palette
        vec3 veinColor = palette(r * contrast + 0.25, palA, palB, palC, palD);
        col = mix(col, veinColor, veins * veinDetail);

        // ** Stellar Spark Points (treble) **
        // Treble spawns bright star-like points at dense regions of the nebula
        // (where the second FBM layer 'r' peaks). Higher treble = more visible stars.
        if (hasAudio && treble > 0.05) {
            float starThreshold = mix(0.85, 0.55, smoothstep(0.05, 0.5, treble));
            float starIntensity = smoothstep(starThreshold, starThreshold + 0.1, r);
            // Tiny glow: use high-freq noise to scatter slightly
            float sparkle = noise(centeredUV * 30.0 + time * 2.0);
            sparkle = smoothstep(0.6, 0.9, sparkle);
            float starBright = starIntensity * sparkle * treble * sparkleStr * audioReact;
            // Stars glow white-hot with a slight palette tint
            vec3 starColor = mix(vec3(1.0), palette(r * contrast + 0.5, palA, palB, palC, palD), 0.2);
            col += starColor * starBright;
        }

        // ── Vitality: highlighted vs dormant ─────────────────
        // Subtle difference: dormant is slightly cooler/dimmer, not a complete change
        if (isHighlighted) {
            col *= 1.08;
        } else {
            float lum = dot(col, vec3(0.299, 0.587, 0.114));
            col = mix(col, vec3(lum), 0.15);
            col *= 0.82 + idlePulse * 0.08;
        }

        // ── Inner edge glow / bevel ─────────────────────────
        // d is negative inside; closer to 0 = closer to edge
        float innerDist = -d; // positive distance from edge inward

        // Darken toward center for depth (original edge fade, improved)
        float depthDarken = smoothstep(0.0, -edgeFadeStart, innerDist);
        col *= mix(0.65, 1.0, 1.0 - depthDarken * 0.3);

        // Bright inner glow near the border (exponential falloff from edge)
        float innerGlow = exp(-innerDist / 14.0);
        vec3 glowColor = palette(r * contrast + 0.1, palA, palB, palC, palD);
        float glowStrength = innerGlowStr;
        col += glowColor * innerGlow * glowStrength;

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // Border
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {

        // Animated border using the same palette (sin/cos avoids atan seam at ±PI)
        float angle = atan(p.y, p.x) * 2.0;
        float borderFlow = fbm(vec2(sin(angle), cos(angle)) * 2.0 + time * 0.5, 3);
        vec3 borderCol = palette(borderFlow * contrast, palA, palB, palC, palD);
        borderCol *= borderBrightness;

        // Highlighted border is brighter and pulses with cosmic breath
        if (isHighlighted) {
            float breathe = 0.85 + 0.15 * sin(time * 2.5);
            // Bass sends a shockwave pulse along the border
            float borderBass = hasAudio ? 1.0 + smoothstep(0.1, 0.4, bass) * 0.25 : 1.0;
            borderCol *= breathe * borderBass;
        } else {
            float lum = dot(borderCol, vec3(0.299, 0.587, 0.114));
            borderCol = mix(borderCol, vec3(lum), 0.3);
            borderCol *= 0.6;
        }

        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }

    // Outer glow -- bass shockwaves expand the glow radius
    float bassGlowPush = hasAudio ? smoothstep(0.05, 0.35, bass) * 18.0 : idlePulse * 5.0;
    float glowRadius = mix(12.0, 20.0, vitality) + bassGlowPush;
    if (d > 0.0 && d < glowRadius && borderGlow > 0.01) {
        float glow = expGlow(d, 8.0, borderGlow);

        // Glow color from palette
        float angle = atan(p.y, p.x);
        vec3 glowCol = palette(angle / TAU + time * 0.1, palA, palB, palC, palD);

        // Highlighted: stronger, vivid glow; dormant: dim
        glowCol *= mix(0.3, 1.0, vitality);

        result.rgb += glowCol * glow * 0.5;
        result.a = max(result.a, glow * 0.4);
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
    float overall = getOverall();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderCosmicZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);

    fragColor = clampFragColor(color);
}
