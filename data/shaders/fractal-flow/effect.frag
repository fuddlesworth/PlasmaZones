// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>

/*
 * FRACTAL FLOW - Shadertoy Conversion
 * Organic fractal-like flowing pattern with iterative distortion
 * Adapted from Shadertoy: void mainImage(out vec4 o, vec2 u)
 *
 * Parameters:
 *   customParams[0].x = fillOpacity (0.3-0.95) - Zone fill opacity
 *   customParams[0].y = speed (0.5-2.0) - Animation speed
 *   customParams[0].z = scale (0.1-0.5) - Coordinate scale (larger = more zoomed out)
 *   customParams[1].x = iterations (10.0-25.0) - Loop iterations (more = denser detail)
 *   customColors[0] = primary tint - Colorizes the effect
 */

// Shadertoy-derived: iterative distortion loop. iterCount controls detail (more = denser).
// Constants (7, 5, 1.5, 9, 11, 40, 1e2, 25.6, 164, 250, 1.35) are from original; tune speed,
// coordScale and iterCount for user control. Loop is dynamic so very high iterCount can be costly.
vec4 sampleFractalFlow(vec2 localFragCoord, vec2 zoneSize, float iTime, float coordScale, float iterCount) {
    vec2 v = zoneSize;
    vec2 u = coordScale * (localFragCoord + localFragCoord - v) / v.y;

    vec4 z = vec4(1.0, 2.0, 3.0, 0.0);
    vec4 o = z;

    float a = 0.5;
    float t = iTime;
    for (float i = 1.0; i < iterCount; i += 1.0) {
        t += 1.0;
        a += 0.03;
        v = cos(t - 7.0 * u * pow(a, i)) - 5.0 * u;

        float denom = length((1.0 + i * dot(v, v)) * sin(1.5 * u / (0.5 - dot(u, u)) - 9.0 * u.yx + t));
        o += (1.0 + cos(z + t)) / max(denom, 1e-6);

        u *= mat2(cos(i + 0.02 * t - z.wxzw * 11.0));
        u += tanh(40.0 * dot(u, u) * cos(1e2 * u.yx + t)) / 2e2 + 0.2 * a * u + cos(4.0 / exp(dot(o, o) / 1e2) + t) / 3e2;
    }

    vec4 oClamped = max(o, 0.001);
    o = 25.6 / (min(oClamped, 13.0) + 164.0 / oClamped) - dot(u, u) / 250.0;
    o = clamp(o, 0.0, 1e4);
    return vec4(min(o.rgb * 1.35, 1.0), 1.0);
}

vec4 renderFractalFlowZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 6.0);
    float borderWidth = max(params.y, 2.5);

    float fillOpacity = customParams[0].x > 0.01 ? customParams[0].x : 0.75;
    float speed = customParams[0].y > 0.01 ? customParams[0].y : 1.0;
    float coordScale = customParams[0].z > 0.01 ? customParams[0].z : 0.25;
    float iterCount = customParams[1].x > 1.0 ? customParams[1].x : 22.0;
    float borderGlowStrength = customParams[1].y > 0.01 ? customParams[1].y : 0.4;
    float glowSize = customParams[1].z > 0.5 ? customParams[1].z : 8.0;

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localFragCoord = fragCoord - rectPos;

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec3 tint = colorWithFallback(customColors[0].rgb, vec3(1.0, 1.0, 1.0));

    if (isHighlighted) {
        fillOpacity = min(fillOpacity + 0.1, 0.98);
        speed *= 1.2;
        tint = mix(tint, vec3(1.0), 0.2);
    }

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        vec4 raw = sampleFractalFlow(localFragCoord, rectSize, iTime * speed, coordScale, iterCount);
        result.rgb = raw.rgb * tint;
        result.a = fillOpacity;
    }

    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        vec3 borderClr = length(borderColor.rgb) > 0.01 ? borderColor.rgb : tint;
        result.rgb = mix(result.rgb, borderClr, border);
        result.a = max(result.a, border * 0.98);
    }

    if (isHighlighted && d > 0.0 && d < 20.0) {
        float glow = expGlow(d, glowSize, borderGlowStrength);
        result.rgb += tint * glow;
        result.a = max(result.a, glow * 0.5);
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

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0)
            continue;

        vec4 zoneColor = renderFractalFlowZone(fragCoord, rect, zoneFillColors[i], zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);

    fragColor = clampFragColor(color);
}
