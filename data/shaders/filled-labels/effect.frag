// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform ZoneUniforms {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int zoneCount;
    int highlightedCount;
    vec4 iMouse;
    vec4 customParams[4];
    vec4 customColors[8];
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

/*
 * FILLED LABELS - Shade inside the zone numbers
 *
 * Uses the label texture only as a mask (alpha). To avoid anti-aliasing issues:
 * fill is applied only where alpha is above a threshold (hardened mask); the
 * original alpha is still used for compositing so the shape edge stays smooth.
 *
 * Parameters:
 *   customParams[0].x = fillOpacity, .y = borderWidth
 *   customParams[1].x = alphaCutoff - only fill where alpha >= this (0.35-0.65)
 *   customParams[1].y = gradient direction: 0 = vertical, 1 = horizontal, 2 = radial
 *   customParams[1].z = pulse speed (0 = static)
 */

#include <common.glsl>

vec4 renderMinimalistZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 10.0);
    float fillOpacity = customParams[0].x > 0.1 ? customParams[0].x : 0.85;
    float borderWidth = customParams[0].y > 0.1 ? customParams[0].y : 2.0;

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec3 cardColor = colorWithFallback(customColors[0].rgb, fillColor.rgb);
    cardColor = colorWithFallback(cardColor, vec3(0.18, 0.2, 0.25));
    vec3 accentColor = colorWithFallback(customColors[1].rgb, vec3(0.55, 0.36, 0.96));
    vec3 borderClr = colorWithFallback(borderColor.rgb, mix(cardColor, vec3(1.0), 0.3));
    if (isHighlighted) {
        cardColor = mix(cardColor, accentColor, 0.25);
        borderClr = accentColor;
        fillOpacity = min(fillOpacity + 0.1, 0.98);
    }

    vec4 result = vec4(0.0);
    if (d < 0.0) {
        result.rgb = cardColor * (1.0 + (1.0 - localUV.y) * 0.08);
        result.a = fillOpacity;
    }
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        result.rgb = mix(result.rgb, borderClr, border);
        result.a = max(result.a, border * 0.95);
    }
    if (isHighlighted && d > 0.0 && d < 15.0) {
        float glow = expGlow(d, 6.0, 0.5);
        result.rgb += accentColor * glow;
        result.a = max(result.a, glow * 0.6);
    }
    return result;
}

vec4 compositeLabelsFilled(vec4 color, vec2 uv) {
    // Mask only: use alpha for coverage. Do not use label RGB.
    float labelAlpha = texture(uZoneLabels, uv).a;
    if (labelAlpha < 0.001) {
        return color;
    }

    // Harden the fill mask so gradient is only inside the character; avoids AA halo.
    // inFill = 1 inside glyph, 0 outside, narrow transition to avoid jagged edges.
    float cutoff = customParams[1].x > 0.0 ? customParams[1].x : 0.45;
    float soft = 0.12;
    float inFill = smoothstep(cutoff - soft, cutoff + soft, labelAlpha);

    float dirMode = customParams[1].y >= 0.0 ? customParams[1].y : 0.0;
    float pulseSpeed = customParams[1].z >= 0.0 ? customParams[1].z : 0.0;

    vec3 startColor = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.95, 0.9));
    vec3 endColor = colorWithFallback(customColors[2].rgb, vec3(0.4, 0.35, 0.5));

    float t;
    if (dirMode < 0.5) {
        t = uv.y;
    } else if (dirMode < 1.5) {
        t = uv.x;
    } else {
        t = length(uv - vec2(0.5));
        t = t * 1.4;
        t = clamp(t, 0.0, 1.0);
    }

    if (pulseSpeed > 0.0) {
        float pulse = 0.85 + 0.15 * sin(iTime * pulseSpeed);
        t = t * pulse + (1.0 - pulse) * 0.5;
        t = clamp(t, 0.0, 1.0);
    }

    vec3 fill = mix(startColor, endColor, t);
    // Only show gradient where inFill is high; at AA edge show blend of background and fill.
    vec3 labelRGB = mix(color.rgb, fill, inFill);
    // Keep original alpha for compositing so the shape boundary stays smooth.
    color.rgb = color.rgb * (1.0 - labelAlpha) + labelRGB * labelAlpha;
    color.a = labelAlpha + color.a * (1.0 - labelAlpha);
    return color;
}

void main() {
    vec2 fragCoord = fragCoordFromTexCoord(vTexCoord);
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        vec4 zoneColor = renderMinimalistZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsFilled(color, labelsUv(fragCoord));

    fragColor = clampFragColor(color);
}
