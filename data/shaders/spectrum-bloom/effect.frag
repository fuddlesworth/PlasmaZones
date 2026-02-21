// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

/*
 * SPECTRUM BLOOM — Polar Spectrum Contour
 *
 * The zone boundary morphs with the full frequency spectrum. Angle from center
 * maps to frequency (bass at one side, treble at the other); radius at that
 * angle = spectrum amplitude. One morphing blob defined by all 256 bars.
 * Best with 256 spectrum bars in KCM settings.
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
 */

// ─── Per-zone rendering ───────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    // Parameters with defaults (sentinel: -1.0 = unset → use default)
    float reactivity   = customParams[0].x >= 0.0 ? customParams[0].x : 1.5;
    float contourScale = customParams[0].y >= 0.0 ? customParams[0].y : 0.4;
    float baseRadius   = customParams[0].z >= 0.0 ? customParams[0].z : 0.12;
    float glowWidth    = customParams[0].w >= 0.0 ? customParams[0].w : 0.04;
    float idleAnim     = customParams[1].x >= 0.0 ? customParams[1].x : 1.0;

    // Zone geometry
    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Colors
    vec3 primary = colorWithFallback(customColors[0].rgb, vec3(0.0, 0.9, 1.0));
    vec3 accent  = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.2, 0.7));

    // ── Highlighted vs dormant ─────────────────────────────────────
    // Highlighted: vivid, bright, responsive. Dormant: desaturated, dim, sluggish.
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

    // Polar coords: angle → frequency u [0,1], r = normalized radius
    float angle = atan(p.y, p.x);
    float u     = fract(angle / TAU + 0.5);  // 0–1, bass→treble around circle
    float refSize = min(rectSize.x, rectSize.y) * 0.5;
    float r     = length(p) / max(refSize, 1.0);

    bool hasAudio = iAudioSpectrumSize > 0;

    // Seam fix: u jumps 1→0 at angle ±π (left). Blend spectrum and color at the seam.
    float seamBlend = 1.0 - smoothstep(0.0, 0.06, min(u, 1.0 - u));
    float uSmooth = mix(u, 0.5, seamBlend);
    float specValRaw = hasAudio ? audioBarSmooth(u) * reactivity : 0.0;
    float specValSeam = hasAudio ? 0.5 * (audioBarSmooth(0.0) + audioBarSmooth(1.0)) * reactivity : 0.0;
    float specVal = mix(specValRaw, specValSeam, seamBlend);

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
        // Fill: inside contour
        float fill = 1.0 - smoothstep(-glowWidth * 0.5, glowWidth * 0.5, distToContour);
        vec3 fillColor_rgb = mix(primary, accent, uSmooth);
        float fillBright = mix(0.06, 0.22, vitality) + mix(0.04, 0.14, vitality) * (specVal + (hasAudio ? 0.0 : idleAnim * 0.3));
        result.rgb = fillColor_rgb * fillBright * fill;
        result.a   = fill * mix(0.5, 0.9, vitality);

        // Glow: at the contour edge
        float glow = exp(-abs(distToContour) / glowWidth) * mix(0.2, 0.6, vitality) + specVal * mix(0.2, 0.5, vitality);
        vec3 glowColor = mix(primary, accent, uSmooth);
        result.rgb += glowColor * glow * mix(0.8, 1.5, vitality);
        result.a   = max(result.a, glow * mix(0.5, 0.95, vitality));

        // Subtle inner darkening toward center
        float centerFade = 1.0 - exp(-r * 2.0) * 0.3;
        result.rgb *= centerFade;

        // Highlighted only: core pulse when audio is active
        if (isHighlighted && hasAudio && specVal > 0.1) {
            float corePulse = exp(-r * 4.0) * specVal * 0.25;
            result.rgb += mix(primary, accent, uSmooth * 0.5 + 0.5) * corePulse;
        }

        // Highlighted only: slow rotation of the contour (phase shift)
        if (isHighlighted && idleAnim > 0.01) {
            float phase = iTime * 0.5;
            contourR += sin(angle * 3.0 + phase) * 0.02 * idleAnim;
            float distToContourRot = r - contourR;
            float rotGlow = exp(-abs(distToContourRot) / glowWidth) * 0.15 * idleAnim;
            result.rgb += accent * rotGlow;
        }
    }

    // Zone border: stronger on highlighted
    float coreWidth = borderWidth * 0.5;
    float core = softBorder(d, coreWidth);
    if (core > 0.0) {
        vec3 borderCol = mix(primary, accent, 0.5);
        float borderStrength = mix(0.2, 0.6, vitality);
        result.rgb = max(result.rgb, borderCol * core * borderStrength);
        result.a   = max(result.a, core * mix(0.3, 0.7, vitality));
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
