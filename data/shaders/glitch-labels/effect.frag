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
 * GLITCH LABELS - Digital glitch effect on zone numbers
 *
 * Horizontal slice displacement (each strip gets time-varying random offset)
 * and optional scan lines. No chromatic aberration; single-sample distortion.
 *
 * Parameters:
 *   customParams[0].x = fillOpacity
 *   customParams[0].y = borderWidth
 *   customParams[1].x = glitchAmount - horizontal displacement strength (0–0.1)
 *   customParams[1].y = glitchSpeed - how fast slices change
 *   customParams[1].z = sliceCount - number of horizontal slices (8–64)
 *   customParams[1].w = scanLineStrength - dark line intensity (0–0.5)
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

// Glitch: slice-based horizontal displacement + optional scan lines (no chroma)
vec4 compositeLabelsGlitch(vec4 color, vec2 uv) {
    vec2 res = max(iResolution, vec2(0.0001));

    float glitchAmount = customParams[1].x > 0.0 ? customParams[1].x : 0.035;
    float glitchSpeed = customParams[1].y > 0.1 ? customParams[1].y : 8.0;
    float sliceCount = customParams[1].z > 1.0 ? customParams[1].z : 24.0;
    float scanLineStrength = customParams[1].w >= 0.0 ? customParams[1].w : 0.15;

    // Quantize V into slices; each slice gets a random horizontal offset that changes over time
    float sliceId = floor(uv.y * sliceCount);
    float t = floor(iTime * glitchSpeed);
    float offset = (hash11(sliceId * 7.1 + t * 13.7) * 2.0 - 1.0) * glitchAmount;
    // Occasional "big" glitch on random slices
    float big = step(0.92, hash11(sliceId * 3.0 + t * 2.0));
    offset += big * (hash11(sliceId * 11.0 + t) * 2.0 - 1.0) * glitchAmount * 1.5;

    vec2 uvGlitch = uv + vec2(offset, 0.0);
    uvGlitch = clamp(uvGlitch, vec2(0.0), vec2(1.0));

    vec4 labels = texture(uZoneLabels, uvGlitch);

    // Scan lines: subtle horizontal dark lines (no chroma), in UV space
    float scan = 1.0 - scanLineStrength * (0.5 + 0.5 * sin(uv.y * PI * res.y * 0.4));
    labels.rgb *= scan;
    // Keep alpha so compositing still works
    labels.a = min(labels.a, 1.0);

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

    color = compositeLabelsGlitch(color, labelsUv(fragCoord));

    fragColor = clampFragColor(color);
}
