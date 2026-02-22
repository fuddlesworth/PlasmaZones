// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

/*
 * SPECTRUM BLOOM — Polar Spectrum Contour (Full-screen with zone cutout)
 *
 * A SINGLE continuous polar contour is centered on the screen center.
 * Zones act as windows into this shared pattern, following the prismata
 * pattern of full-screen rendering with zone cutout. Angle from screen
 * center maps to frequency (bass at one side, treble at the other);
 * radius at that angle = spectrum amplitude. One morphing blob defined
 * by all 256 bars. Best with 256 spectrum bars in KCM settings.
 *
 * Parameters (customParams):
 *   [0].x = reactivity       — audio sensitivity (0.5–3)
 *   [0].y = contourScale     — how far spectrum extends from base (0.2–0.6)
 *   [0].z = baseRadius       — minimum contour radius when silent (0.05–0.3)
 *   [0].w = glowWidth       — width of contour glow (0.02–0.1)
 *   [1].x = idleAnimation    — star pulse when no audio (0–2)
 *
 * Colors:
 *   customColors[0] = primary (low freq, default: cyan)
 *   customColors[1] = accent  (high freq, default: magenta)
 *   customColors[2] = warm    (bass/core, default: amber)
 *   customColors[3] = mid     (mid freq, default: green)
 *   customColors[4] = cool    (nebula tint, default: deep violet)
 */



// ─── Per-zone rendering ───────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted,
                float bass, float mids, float treble, float overall, bool hasAudio)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    // Parameters with defaults (sentinel: -1.0 = unset -> use default)
    float reactivity   = customParams[0].x >= 0.0 ? customParams[0].x : 1.5;
    float contourScale = customParams[0].y >= 0.0 ? customParams[0].y : 0.4;
    float baseRadius   = customParams[0].z >= 0.0 ? customParams[0].z : 0.12;
    float glowWidth    = customParams[0].w >= 0.0 ? customParams[0].w : 0.04;
    float idleAnim     = customParams[1].x >= 0.0 ? customParams[1].x : 1.0;
    float nebulaStr    = customParams[1].y >= 0.0 ? customParams[1].y : 0.20;
    float coreStr      = customParams[1].z >= 0.0 ? customParams[1].z : 0.20;
    float auroraStr    = customParams[1].w >= 0.0 ? customParams[1].w : 0.15;
    float sparkleScale = customParams[2].x >= 0.0 ? customParams[2].x : 1.0;
    float fillOpacity  = customParams[2].y >= 0.0 ? customParams[2].y : 0.85;

    // Zone geometry -- KEEP for cutout, border, edge effects
    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;  // KEEP for border/glow angle
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Screen-space polar coords
    vec2 screenCenter = iResolution.xy * 0.5;
    vec2 sp = fragCoord - screenCenter;
    float angle = atan(sp.y, sp.x) + PI * 0.5; // 90° CW so bass starts at top
    float u     = fract(angle / TAU + 0.5);
    float refSize = min(iResolution.x, iResolution.y) * 0.5;
    float r     = length(sp) / max(refSize, 1.0);

    // Screen-space UV
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));

    // Colors — 5-color palette for richer visuals
    vec3 primary = colorWithFallback(customColors[0].rgb, vec3(0.0, 0.9, 1.0));
    vec3 accent  = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.2, 0.7));
    vec3 warm    = colorWithFallback(customColors[2].rgb, vec3(1.0, 0.6, 0.1));
    vec3 midCol  = colorWithFallback(customColors[3].rgb, vec3(0.2, 1.0, 0.4));
    vec3 cool    = colorWithFallback(customColors[4].rgb, vec3(0.4, 0.1, 0.9));

    // ── Highlighted vs dormant ─────────────────────────────────────
    float vitality = isHighlighted ? 1.0 : 0.25;

    if (!isHighlighted) {
        primary = mix(primary, vec3(dot(primary, vec3(0.299, 0.587, 0.114))), 0.6);
        accent  = mix(accent, vec3(dot(accent, vec3(0.299, 0.587, 0.114))), 0.6);
        reactivity   *= 0.5;
        contourScale *= 0.55;
        glowWidth    *= 0.7;
        idleAnim     *= 0.4;
    } else {
        reactivity   *= 1.4;
        contourScale *= 1.1;
        glowWidth    *= 1.2;
    }

    float energy  = hasAudio ? overall * reactivity : 0.0;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(iTime * 0.8 * PI)) * idleAnim;

    // Smooth cyclic color parameter: cos(angle) wraps continuously at ±pi (left side)
    // so there's no hard color edge. 0 at right, 1 at left, smooth everywhere.
    float colorT = 0.5 - 0.5 * cos(angle);

    // Spectrum sampling with smooth seam: blend a short arc at the wrap point
    // so the contour shape doesn't have a hard jump at the left side.
    float specValRaw = hasAudio ? audioBarSmooth(u) * reactivity : 0.0;
    float seamDist = min(u, 1.0 - u); // distance to the seam (0 at seam)
    float seamW = smoothstep(0.0, 0.03, seamDist); // narrow blend zone
    float specValWrap = hasAudio ? mix(audioBarSmooth(0.0), audioBarSmooth(1.0), 0.5) * reactivity : 0.0;
    float specVal = mix(specValWrap, specValRaw, seamW);

    // Contour radius at this angle: full 256-bar spectrum defines the shape
    float contourR = baseRadius + specVal * contourScale;

    // Idle: star-like pulse when silent
    if (!hasAudio && idleAnim > 0.01) {
        float star = 0.5 + 0.5 * sin(angle * 5.0 + iTime * 0.8);
        contourR += star * 0.12 * idleAnim;
    }

    // Distance from fragment to contour (positive = outside contour)
    float distToContour = r - contourR;

    vec4 result = vec4(0.0);

    // Only render inside zone
    if (d < 0.0) {

        // ── Warped noise nebula background ─────────────────────
        // Fills the entire zone interior with animated nebula clouds.
        // Audio drives warp intensity and brightness.
        {
            vec2 nebUV = globalUV * 2.5;
            float warpAmt = 0.3 + energy * 0.6 + idlePulse * 0.2;
            float n1 = noise2D(nebUV + iTime * 0.12);
            float n2 = noise2D(nebUV * 1.7 - iTime * 0.08 + 30.0);
            vec2 warp = vec2(n1, n2) * warpAmt;
            nebUV += warp;

            // Multi-octave noise
            float neb = 0.0;
            float amp = 0.5;
            vec2 octUV = nebUV;
            for (int o = 0; o < 4; o++) {
                neb += noise2D(octUV) * amp;
                octUV *= 2.1;
                amp *= 0.5;
            }

            // Color: 5-way palette blend across the nebula, driven by noise + radius
            float colorPhase = neb + r * 0.4 + iTime * 0.06;
            // Cycle through all 5 colors using phase
            float cp = fract(colorPhase * 0.5);
            vec3 nebColor;
            if (cp < 0.2) {
                nebColor = mix(cool, primary, cp * 5.0);
            } else if (cp < 0.4) {
                nebColor = mix(primary, midCol, (cp - 0.2) * 5.0);
            } else if (cp < 0.6) {
                nebColor = mix(midCol, warm, (cp - 0.4) * 5.0);
            } else if (cp < 0.8) {
                nebColor = mix(warm, accent, (cp - 0.6) * 5.0);
            } else {
                nebColor = mix(accent, cool, (cp - 0.8) * 5.0);
            }
            nebColor *= 0.7;
            // Bass-reactive core tinting with warm color
            if (hasAudio) {
                nebColor = mix(nebColor, warm, exp(-r * 2.5) * bass * 0.5);
            }

            // Brightness: always visible base + audio boost
            float nebBase = mix(nebulaStr * 0.5, nebulaStr * 1.4, vitality) + idlePulse * 0.12;
            float nebAudio = energy * mix(0.10, 0.35, vitality);
            float nebBright = neb * (nebBase + nebAudio);
            nebBright *= 1.0 - smoothstep(0.0, 1.3, r) * 0.25;

            result.rgb = nebColor * nebBright;
            result.a = mix(fillOpacity * 0.59, fillOpacity, vitality);
        }

        // ── Warm central core glow ─────────────────────────────
        // A breathing warm glow at the center that responds to overall energy.
        {
            float coreIntensity = mix(coreStr * 0.4, coreStr * 1.25, vitality) + energy * mix(0.10, 0.35, vitality) + idlePulse * 0.10;
            // Breathing modulation
            float breathe = 0.85 + 0.15 * sin(iTime * 1.5 + energy * 3.0);
            coreIntensity *= breathe;
            float coreFalloff = exp(-r * mix(3.0, 2.2, vitality));
            vec3 coreColor = mix(primary, warm, 0.3 + 0.2 * sin(iTime * 0.5));
            // Warm tint toward the very center
            coreColor = mix(coreColor, warm, exp(-r * 5.0) * 0.4);
            result.rgb += coreColor * coreFalloff * coreIntensity;
        }

        // ── Contour fill and glow (existing, refined) ──────────
        {
            float fill = 1.0 - smoothstep(-glowWidth * 0.5, glowWidth * 0.5, distToContour);
            // 5-color gradient around the contour
            float ct5 = colorT * 4.0;
            vec3 fillColor_rgb;
            if (ct5 < 1.0) fillColor_rgb = mix(primary, midCol, ct5);
            else if (ct5 < 2.0) fillColor_rgb = mix(midCol, warm, ct5 - 1.0);
            else if (ct5 < 3.0) fillColor_rgb = mix(warm, accent, ct5 - 2.0);
            else fillColor_rgb = mix(accent, cool, ct5 - 3.0);
            float fillBright = mix(0.08, 0.25, vitality) + mix(0.05, 0.18, vitality) * (specVal + (hasAudio ? 0.0 : idleAnim * 0.3));
            result.rgb += fillColor_rgb * fillBright * fill;
            result.a = max(result.a, fill * mix(0.5, 0.9, vitality));

            // Glow at the contour edge
            float glow = exp(-abs(distToContour) / glowWidth) * mix(0.25, 0.7, vitality) + specVal * mix(0.2, 0.5, vitality);
            // Glow follows the same 5-color contour gradient
            vec3 glowColor;
            if (ct5 < 1.0) glowColor = mix(primary, midCol, ct5);
            else if (ct5 < 2.0) glowColor = mix(midCol, warm, ct5 - 1.0);
            else if (ct5 < 3.0) glowColor = mix(warm, accent, ct5 - 2.0);
            else glowColor = mix(accent, cool, ct5 - 3.0);
            result.rgb += glowColor * glow * mix(0.8, 1.5, vitality);
            result.a = max(result.a, glow * mix(0.5, 0.95, vitality));
        }

        // ── Aurora streaks radiating from center ───────────────
        // Radial lines emanating outward, modulated by noise for a flowing look.
        {
            float auroraCount = 12.0;
            float auroraFlow = iTime * 0.4;
            // Angular noise creates flowing streaks
            float aN = angularNoise(angle, auroraCount, auroraFlow);
            // Sharpen the noise into distinct streaks
            float streaks = pow(aN, 3.0);
            // Radial falloff: strongest mid-range, fading at center and far edge
            float radialMask = smoothstep(0.05, 0.25, r) * (1.0 - smoothstep(0.6, 1.1, r));
            // Audio modulation: mids drive aurora intensity
            float auroraEnergy = mix(auroraStr * 0.53, auroraStr * 1.33, vitality) + (hasAudio ? mids * mix(0.15, 0.45, vitality) : idlePulse * 0.1);
            // Second layer of streaks at different frequency for depth
            float aN2 = angularNoise(angle, auroraCount * 0.6, -auroraFlow * 0.7 + 50.0);
            float streaks2 = pow(aN2, 3.0);

            vec3 auroraCol1 = mix(midCol, cool, 0.3 + 0.3 * sin(angle * 2.0 + iTime * 0.3));
            vec3 auroraCol2 = mix(warm, accent, 0.4 + 0.3 * cos(angle * 1.5 - iTime * 0.2));

            result.rgb += auroraCol1 * streaks * radialMask * auroraEnergy;
            result.rgb += auroraCol2 * streaks2 * radialMask * auroraEnergy * 0.6;
        }

        // ── Radial particle sparkles along contour edge ────────
        // Small bright dots positioned near the contour boundary.
        {
            float sparkleZone = exp(-pow(distToContour / (glowWidth * 2.0), 2.0));
            if (sparkleZone > 0.01) {
                float sparkleIntensity = 0.0;
                vec3 sparkleColor = vec3(0.0);
                // Place sparkle particles at angular intervals
                for (int s = 0; s < 24; s++) {
                    float sAngle = float(s) / 24.0 * TAU;
                    // Jitter the sparkle position with noise
                    float jitter = noise1D(float(s) * 7.3 + iTime * 2.0) * 0.15;
                    float sAngleJ = sAngle + jitter;
                    // Get spectrum value at this sparkle's angle
                    float sU = fract((sAngleJ + PI * 0.5) / TAU + 0.5);
                    float sSpec = hasAudio ? audioBarSmooth(sU) * reactivity : idlePulse * 0.3;
                    // Sparkle radius matches the contour at its angle
                    float sBaseR = baseRadius + sSpec * contourScale;
                    float sR = sBaseR + noise1D(float(s) * 13.7 + iTime * 3.0) * 0.03;
                    // Sparkle world position
                    vec2 sPos = vec2(cos(sAngleJ), sin(sAngleJ)) * sR * refSize;
                    float sDist = length(sp - sPos) / max(refSize, 1.0);
                    // Sharp bright point
                    float sparkle = exp(-sDist * sDist / (0.0004 * mix(0.5, 1.5, vitality)));
                    // Brightness driven by local spectrum peak
                    float brightness = sSpec * mix(0.3, 1.2, vitality);
                    // Twinkle
                    float twinkle = 0.5 + 0.5 * sin(iTime * 8.0 + float(s) * 2.7);
                    sparkle *= twinkle * brightness;

                    // Sparkle color from 5-color palette based on angle
                    float su5 = sU * 4.0;
                    vec3 sCol;
                    if (su5 < 1.0) sCol = mix(primary, midCol, su5);
                    else if (su5 < 2.0) sCol = mix(midCol, warm, su5 - 1.0);
                    else if (su5 < 3.0) sCol = mix(warm, accent, su5 - 2.0);
                    else sCol = mix(accent, cool, su5 - 3.0);
                    // White-hot core
                    sCol = mix(sCol, vec3(1.0), sparkle * 0.4);

                    sparkleIntensity += sparkle;
                    sparkleColor += sCol * sparkle;
                }
                if (sparkleIntensity > 0.01) {
                    sparkleColor /= sparkleIntensity;
                    sparkleIntensity = min(sparkleIntensity, 2.0);
                    result.rgb += sparkleColor * sparkleIntensity * sparkleZone;
                }
            }
        }

        // ── Highlighted: slow rotation phase shift ─────────────
        if (isHighlighted && idleAnim > 0.01) {
            float phase = iTime * 0.5;
            float rotContourR = contourR + sin(angle * 3.0 + phase) * 0.02 * idleAnim;
            float distToContourRot = r - rotContourR;
            float rotGlow = exp(-abs(distToContourRot) / glowWidth) * 0.15 * idleAnim;
            result.rgb += accent * rotGlow;
        }

        // Subtle inner darkening toward center (gentle, doesn't kill the core glow)
        float centerFade = 1.0 - exp(-r * 3.0) * 0.15;
        result.rgb *= centerFade;
    }

    // ── Zone border: flowing energy ──────────────────────────────
    {
        float coreWidth = borderWidth * mix(0.5, 0.9, vitality);
        float core = softBorder(d, coreWidth);
        if (core > 0.0) {
            float bAngle = atan(p.x, -p.y) / TAU + 0.5;

            // Audio-reactive border energy
            float borderEnergy = 1.0 + energy * mix(0.2, 1.0, vitality) + idlePulse * 0.3;
            vec3 coreColor = primary * mix(0.6, 1.8, vitality) * borderEnergy;

            // Flowing highlights: animated angular noise
            float flowSpeed = mix(0.3, 2.0, vitality);
            float flowRange = mix(0.1, 0.4, vitality);
            float flow = angularNoise(bAngle, 10.0, -iTime * flowSpeed) * flowRange + (1.0 - flowRange * 0.5);
            coreColor *= flow;

            // Highlighted: pulsing and accent trace
            if (isHighlighted) {
                float breathe = 0.8 + 0.2 * sin(iTime * 3.0 + energy * 4.0);
                coreColor *= breathe;
                float accentTrace = angularNoise(bAngle, 6.0, iTime * 2.5);
                coreColor = mix(coreColor, accent * mix(0.6, 1.8, vitality) * borderEnergy, accentTrace * 0.4);
            }

            // White-hot center of the border line
            coreColor = mix(coreColor, vec3(1.0), core * mix(0.25, 0.6, vitality));

            // Bass flash
            if (hasAudio && bass > 0.5) {
                float flash = (bass - 0.5) * 2.0 * vitality;
                vec3 bassWarm = mix(accent, vec3(1.0, 0.5, 0.2), 0.4);
                coreColor = mix(coreColor, bassWarm * 2.0, flash * core * 0.3);
            }

            result.rgb = max(result.rgb, coreColor * core);
            result.a = max(result.a, core);
        }
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
