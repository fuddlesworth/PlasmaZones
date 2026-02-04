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
 * CRYSTALLINE LABELS - Voronoi fractured zone numbers with stained-glass edges
 *
 * Zone numbers are split into Voronoi cells; each cell drifts with time.
 * Optional bright lines at cell boundaries (stained-glass / circuit-board look).
 * No chromatic aberration. Unique showcase effect.
 *
 * Parameters:
 *   customParams[0].x = fillOpacity, .y = borderWidth
 *   customParams[1].x = cellScale (cells per axis), .y = driftAmount, .z = driftSpeed
 *   customParams[1].w = edgeStrength (0 = no lines), customParams[2].x = edgeWidth
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

// Voronoi: returns (dist_to_nearest, dist_to_second, cell_id_for_animation)
vec3 voronoi(vec2 uv, float scale) {
    vec2 p = uv * scale;
    vec2 n = floor(p);
    float d1 = 1e5, d2 = 1e5;
    float cellId = 0.0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 g = n + vec2(float(i), float(j));
            vec2 c = g + hash22(g);
            float d = length(p - c);
            if (d < d1) {
                d2 = d1;
                d1 = d;
                cellId = hash21(g);
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    return vec3(d1, d2, cellId);
}

vec4 compositeLabelsCrystalline(vec4 color, vec2 uv) {
    float cellScale = customParams[1].x > 1.0 ? customParams[1].x : 14.0;
    float driftAmount = customParams[1].y >= 0.0 ? customParams[1].y : 0.018;
    float driftSpeed = customParams[1].z > 0.1 ? customParams[1].z : 1.8;
    float edgeStrength = customParams[1].w >= 0.0 ? customParams[1].w : 0.7;
    float edgeWidth = customParams[2].x > 0.0 ? customParams[2].x : 0.015;

    vec3 vo = voronoi(uv, cellScale);
    float d1 = vo.x, d2 = vo.y;
    float cellId = vo.z;

    // Per-cell drift (each crystal shard moves slightly)
    vec2 offset = driftAmount * vec2(
        sin(cellId * TAU + iTime * driftSpeed),
        cos(cellId * 7.131 + iTime * driftSpeed * 1.07)
    );
    vec2 uvDrift = uv + offset;
    uvDrift = clamp(uvDrift, vec2(0.0), vec2(1.0));

    vec4 labels = texture(uZoneLabels, uvDrift);

    // Stained-glass edge: bright line at Voronoi boundaries
    float gap = d2 - d1;
    float edge = edgeStrength * (1.0 - smoothstep(0.0, edgeWidth, gap));
    vec3 edgeColor = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.95, 0.9));
    labels.rgb = mix(labels.rgb, edgeColor, edge);
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

    color = compositeLabelsCrystalline(color, labelsUv(fragCoord));

    fragColor = clampFragColor(color);
}
