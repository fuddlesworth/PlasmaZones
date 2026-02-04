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
    vec4 iMouse;        // xy = pixels, zw = normalized (0-1)
    vec4 customParams[4];  // [0-3], access as customParams[0].x for slot 0, etc.
    vec4 customColors[8];  // [0-7], access as customColors[0] for color slot 0, etc.
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

/*
 * WARPED LABELS - Minimalist zones with distorted zone numbers
 *
 * Applies sine wave UV distortion and chromatic aberration to the
 * pre-rendered zone number texture, demonstrating texture pass manipulation.
 *
 * Parameters:
 *   customParams[0].x = fillOpacity
 *   customParams[0].y = borderWidth
 *   customParams[1].x = waveAmount - UV wave distortion strength
 *   customParams[1].y = waveSpeed - Animation speed
 *   customParams[1].z = chromaStrength - RGB channel separation
 *   customParams[1].w = waveFreq - Wave frequency
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

// Warped labels: sine wave distortion + chromatic aberration
vec4 compositeLabelsWarped(vec4 color, vec2 uv) {
    float waveAmount = customParams[1].x > 0.0001 ? customParams[1].x : 0.02;
    float waveSpeed = customParams[1].y > 0.1 ? customParams[1].y : 2.5;
    float chromaStrength = customParams[1].z > 0.0 ? customParams[1].z : 0.008;
    float waveFreq = customParams[1].w > 0.5 ? customParams[1].w : 4.0;

    // Sine wave distortion: offset UV based on vertical position and time
    float wave = sin(uv.y * PI * waveFreq + iTime * waveSpeed) * waveAmount;
    vec2 uvWarped = uv + vec2(wave, 0.0);

    // Chromatic aberration: sample R, G, B at slightly different horizontal offsets
    vec2 uvR = uvWarped + vec2(chromaStrength, 0.0);
    vec2 uvG = uvWarped;
    vec2 uvB = uvWarped - vec2(chromaStrength, 0.0);

    vec4 labelsR = texture(uZoneLabels, uvR);
    vec4 labelsG = texture(uZoneLabels, uvG);
    vec4 labelsB = texture(uZoneLabels, uvB);

    vec4 labels = vec4(labelsR.r, labelsG.g, labelsB.b, max(max(labelsR.a, labelsG.a), labelsB.a));

    // Premult alpha over
    color.rgb = color.rgb * (1.0 - labels.a) + labels.rgb;
    color.a = labels.a + color.a * (1.0 - labels.a);

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

    // Warped labels (custom manipulation of zone numbers)
    color = compositeLabelsWarped(color, labelsUv(fragCoord));

    fragColor = clampFragColor(color);
}
