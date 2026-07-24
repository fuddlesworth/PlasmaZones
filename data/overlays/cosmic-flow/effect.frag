// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * COSMIC FLOW - Fragment Shader
 *
 * Renders a SINGLE continuous cosmic flow field across the entire overlay.
 * All pattern/animation uses fragCoord (screen space) so FBM flows seamlessly
 * across zone boundaries -- zones are windows into the shared field.
 * Based on Inigo Quilez's palette technique and classic fbm.
 *
 * Features highlight/dormant vitality system, secondary detail veins,
 * inner edge glow with bevel, and organic audio reactivity (FBM contrast
 * deepening, palette phase rotation, vein widening — no UV warping or
 * shockwave clichés).
 *
 * The harness supplies #version, <common.glsl> (zone UBO + ZoneCtx + helpers),
 * the vTexCoord/vFragCoord ins, and the fragColor out. audio.glsl is
 * pack-specific, so it stays here. A whole-frame label composite runs after
 * the per-zone loop, so this is a pImage entry point.
 */

#include <audio.glsl>


// fbm() (call with gain 0.6 for this pack's softer falloff) and the IQ cosine
// palette (iqPalette) come from common.glsl.

vec4 renderCosmicZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted,
                      float bass, float mids, float treble, bool hasAudio) {
    // Corner radius: logical px to device px, clamped to half the zone's smaller side.
    // Shared with the decoration side via zoneSdf() in shared/common.glsl.
    ZoneSDF zoneShape = zoneSdf(fragCoord, rect, params.x);
    float borderWidth = zoneBorderWidth(params.y);

    // Get shader parameters with defaults
    float speed = p_speed >= 0.0 ? p_speed : 0.1;
    float flowSpeed = p_flowSpeed >= 0.0 ? p_flowSpeed : 0.3;
    float noiseScale = p_noiseScale >= 0.0 ? p_noiseScale : 3.0;
    int octaves = int(p_octaves >= 0.0 ? p_octaves : 6.0);

    float colorShift = p_colorShift >= 0.0 ? p_colorShift : 0.0;
    float saturation = p_saturation >= 0.0 ? p_saturation : 0.5;
    float brightness = p_brightness >= 0.0 ? p_brightness : 0.5;
    float contrast = p_contrast >= 0.0 ? p_contrast : 0.95;

    float fillOpacity = p_fillOpacity >= 0.0 ? p_fillOpacity : 0.85;
    float borderGlow = p_borderGlow >= 0.0 ? p_borderGlow : 0.3;
    float edgeFadeStart = zoneLen(p_edgeFadeStart >= 0.0 ? p_edgeFadeStart : 30.0);
    float borderBrightness = p_borderBrightness >= 0.0 ? p_borderBrightness : 1.3;

    float audioReact = p_audioReactivity >= 0.0 ? p_audioReactivity : 1.0;
    float veinDetail = p_veinDetail >= 0.0 ? p_veinDetail : 0.25;
    float innerGlowStr = p_innerGlowStrength >= 0.0 ? p_innerGlowStrength : 0.25;
    float sparkleStr = p_sparkleIntensity >= 0.0 ? p_sparkleIntensity : 2.5;

    float fbmRot = p_fbmRotation >= 0.0 ? p_fbmRotation : 0.5;

    // Convert rect to pixel coordinates
    vec2 center = zoneShape.center;  // already computed by zoneSdf()

    // Position relative to zone center
    vec2 p = fragCoord - center;

    // Calculate SDF
    float d = zoneShape.d;

    // Global screen-space UV for pattern generation -- one continuous field
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    vec2 centeredUV = (globalUV * 2.0 - 1.0) * noiseScale;

    // Aspect correction (screen-space)
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    centeredUV.x *= aspect;

    // Palette colors from uniforms or defaults
    vec3 palA = p_paletteColorA.rgb;
    vec3 palB = p_paletteColorB.rgb;
    vec3 palC = p_paletteColorC.rgb;
    vec3 palD = p_paletteColorD.rgb;

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

    // ── Audio envelopes ────────────────────────────────────────
    float bassEnv   = hasAudio ? smoothstep(0.02, 0.3, bass) * audioReact : 0.0;
    float midsEnv   = hasAudio ? smoothstep(0.02, 0.4, mids) * audioReact : 0.0;

    // Audio modulates existing animation parameters organically:
    //   bass  → FBM contrast deepening (brighter peaks, darker valleys)
    //   mids  → palette phase rotation (prismatic drift per channel)
    //   treble → stellar sparks (handled below, already unique)
    float audioContrast = 1.0 + bassEnv * 0.2;

    // Mids rotate the palette phase vector per-channel for prismatic drift
    float audioPalRotation = midsEnv * 0.25;
    palD += vec3(audioPalRotation * 0.5, audioPalRotation * 0.3, audioPalRotation * 0.1);

    // Pattern speed is constant across all zones so the full-screen field is continuous.
    // Vitality only affects post-computation brightness/saturation, never the pattern itself.
    float vSpeed = speed;
    float vFlowSpeed = flowSpeed;

    vec4 result = vec4(0.0);
    float time = iTime;

    // Inside the zone
    if (d < 0.0) {

        // First fbm layer - slow drift
        float q = fbm(centeredUV + time * vSpeed, octaves, fbmRot, 0.6);

        // Second fbm layer - combines with first for complexity
        float r = fbm(centeredUV + q + time * vFlowSpeed, octaves, fbmRot, 0.6);

        // Generate color from palette — bass deepens contrast
        vec3 col = iqPalette(r * contrast * audioContrast, palA, palB, palC, palD);

        // ── Secondary detail layer: noise veins ─────────────
        // Thin vein lines at the 0.5 contour of a higher-frequency noise
        // Bass widens the vein detection threshold (physically thicker veins)
        float veinNoise = noise2D(centeredUV * 6.0 + time * vSpeed * 0.5);
        float veinWidth = 0.06 + bassEnv * 0.025;
        float veins = 1.0 - smoothstep(0.0, veinWidth, abs(veinNoise - 0.5));
        // Color veins slightly offset from base palette
        vec3 veinColor = iqPalette(r * contrast * audioContrast + 0.25, palA, palB, palC, palD);
        col = mix(col, veinColor, veins * veinDetail);

        // ** Stellar Spark Points (treble) **
        // Treble spawns bright star-like points at dense regions of the nebula
        // (where the second FBM layer 'r' peaks). Higher treble = more visible stars.
        if (hasAudio && treble > 0.05) {
            float starThreshold = mix(0.85, 0.55, smoothstep(0.05, 0.5, treble));
            float starIntensity = smoothstep(starThreshold, starThreshold + 0.1, r);
            // Tiny glow: use high-freq noise to scatter slightly
            float sparkle = noise2D(centeredUV * 30.0 + time * 2.0);
            sparkle = smoothstep(0.6, 0.9, sparkle);
            float starBright = starIntensity * sparkle * treble * sparkleStr * audioReact;
            // Stars glow white-hot with a slight palette tint
            vec3 starColor = mix(vec3(1.0), iqPalette(r * contrast + 0.5, palA, palB, palC, palD), 0.2);
            col += starColor * starBright;
        }

        // ── Vitality: highlighted vs dormant ─────────────────
        // Subtle difference: dormant is slightly cooler/dimmer, not a complete change
        if (isHighlighted) {
            col *= 1.08;
        } else {
            float lum = luminance(col);
            col = mix(col, vec3(lum), 0.15);
            col *= 0.82 + idlePulse * 0.08;
        }

        // ── Inner edge glow / bevel ─────────────────────────
        // d is negative inside; closer to 0 = closer to edge
        float innerDist = -d; // positive distance from edge inward

        // Darken toward center for depth (original edge fade, improved)
        float depthDarken = smoothstep(0.0, edgeFadeStart, innerDist);
        col *= mix(0.65, 1.0, 1.0 - depthDarken * 0.3);

        // Bright inner glow near the border (exponential falloff from edge)
        float innerGlow = exp(-innerDist / zoneLen(14.0));
        vec3 glowColor = iqPalette(r * contrast + 0.1, palA, palB, palC, palD);
        float glowStrength = innerGlowStr;
        col += glowColor * innerGlow * glowStrength;

        // Zone fill color tint — let per-zone color influence the palette
        col = mix(col, zoneFillHue(fillColor) * luminance(col), 0.2);

        result.rgb = col;
        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);
    }

    // Border
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {

        // Animated border using the same palette (sin/cos avoids atan seam at ±PI)
        float angle = atan(p.y, p.x) * 2.0;
        float borderFlow = fbm(vec2(sin(angle), cos(angle)) * 2.0 + time * 0.5, 3, 0.5, 0.6);
        vec3 borderCol = iqPalette(borderFlow * contrast, palA, palB, palC, palD);
        // Zone border color tint — let per-zone color influence the palette
        vec3 zoneBorderTint = colorWithFallback(borderColor.rgb, borderCol);
        borderCol = mix(borderCol, zoneBorderTint * luminance(borderCol), 0.3);
        borderCol *= borderBrightness;

        // Highlighted border is brighter and pulses with cosmic breath
        if (isHighlighted) {
            float breathe = 0.85 + 0.15 * sin(time * 2.5);
            // Bass sends a shockwave pulse along the border
            float borderBass = hasAudio ? 1.0 + smoothstep(0.1, 0.4, bass) * 0.25 : 1.0;
            borderCol *= breathe * borderBass;
        } else {
            float lum = luminance(borderCol);
            borderCol = mix(borderCol, vec3(lum), 0.3);
            borderCol *= 0.6;
        }

        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }

    // Outer glow
    float bassGlowPush = hasAudio ? bassEnv * 2.0 : idlePulse * 5.0;
    float glowRadius = zoneLen(mix(12.0, 20.0, vitality) + bassGlowPush);
    if (d > 0.0 && d < glowRadius && borderGlow > 0.01) {
        float glow = expGlow(d, zoneLen(8.0), borderGlow);

        // Glow color from palette
        float angle = atan(p.y, p.x);
        float glowT = angularNoise(angle, 1.5, time * 0.1);
        vec3 glowCol = iqPalette(glowT, palA, palB, palC, palD);

        // Highlighted: stronger, vivid glow; dormant: dim
        glowCol *= mix(0.3, 1.0, vitality);

        result.rgb += glowCol * glow * 0.5;
        result.a = max(result.a, glow * 0.4);
    }

    return result;
}

// ─── Custom Label Composite ───────────────────────────────────────

vec4 compositeCosmicLabels(vec4 color, vec2 fragCoord,
                           float bass, float treble, bool hasAudio) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec4 labels = texture(uZoneLabels, uv);

    // Reconstruct IQ palette colors (same defaults as renderCosmicZone)
    float brightness = p_brightness >= 0.0 ? p_brightness : 0.5;
    float saturation = p_saturation >= 0.0 ? p_saturation : 0.5;
    float colorShift = p_colorShift >= 0.0 ? p_colorShift : 0.0;
    vec3 palA = colorWithFallback(p_paletteColorA.rgb, vec3(brightness));
    vec3 palB = colorWithFallback(p_paletteColorB.rgb, vec3(saturation));
    vec3 palC = colorWithFallback(p_paletteColorC.rgb, vec3(1.0));
    vec3 palD = colorWithFallback(p_paletteColorD.rgb, vec3(0.0, 0.10, 0.20));
    palD += vec3(colorShift);

    float labelGlowSpread = p_nebulaSpread >= 0.0 ? p_nebulaSpread : 3.0;
    float labelBrightness = p_lensIntensity >= 0.0 ? p_lensIntensity : 2.5;
    float labelAudioReact = p_stellarReact >= 0.0 ? p_stellarReact : 1.0;

    // Gaussian halo with nebula palette tint
    float halo = 0.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            float w = exp(-float(dx * dx + dy * dy) * 0.3);
            halo += texture(uZoneLabels, uv + vec2(float(dx), float(dy)) * px * labelGlowSpread).a * w;
        }
    }
    halo /= 16.5;

    if (halo > 0.003) {
        float haloEdge = halo * (1.0 - labels.a);

        // Palette-colored halo that drifts with time
        float t = length(uv - 0.5) + iTime * 0.08;
        vec3 haloCol = iqPalette(t, palA, palB, palC, palD);
        float haloBright = haloEdge * (0.5 + (hasAudio ? bass * 0.5 * labelAudioReact : 0.0));
        color.rgb += haloCol * haloBright;

        // Stellar sparks near labels on treble
        if (hasAudio && treble > 0.1) {
            float sparkNoise = noise2D(uv * 50.0 + iTime * 3.0);
            float spark = smoothstep(0.7, 0.95, sparkNoise) * treble * 2.0 * labelAudioReact;
            vec3 sparkCol = mix(vec3(1.0), iqPalette(t + 0.3, palA, palB, palC, palD), 0.3);
            color.rgb += sparkCol * haloEdge * spark;
        }

        color.a = max(color.a, haloEdge * 0.5);
    }

    // Core: amplified nebula lens
    if (labels.a > 0.01) {
        vec3 nebLens = color.rgb * labelBrightness;
        float t = length(uv - 0.5) + iTime * 0.1;
        nebLens += iqPalette(t, palA, palB, palC, palD) * 0.2;
        float bassPulse = hasAudio ? 1.0 + bass * 0.5 * labelAudioReact : 1.0;
        nebLens *= bassPulse;
        color.rgb = mix(color.rgb, nebLens, labels.a);
        color.a = max(color.a, labels.a);
    }

    return color;
}

vec4 pImage(vec2 fragCoord) {
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        return vec4(0.0);
    }

    // Audio analysis (computed once for all zones)
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderCosmicZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, hasAudio);
        color = blendOver(color, zoneColor);
    }

    if (p_showLabels > 0.5)
        color = compositeCosmicLabels(color, fragCoord, bass, treble, hasAudio);

    return color;
}
