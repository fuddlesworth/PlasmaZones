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

layout(binding = 1) uniform sampler2D uZoneLabels;

/*
 * FLOW LABELS - Zone numbers filled with animated flowing plasma (FBM + palette)
 *
 * Zone: Minimal dark card with thin border so the label is the focus.
 * Label: Interior of each digit is shaded with flowing noise (same technique as cosmic-flow);
 *        flow is driven by uv + time so the pattern animates inside the glyph. Optional
 *        bright outline so the digit reads clearly.
 */

#include <common.glsl>

float rand2D(vec2 p) {
    return fract(sin(dot(p, vec2(15.285, 97.258))) * 47582.122);
}

vec2 quintic(vec2 f) {
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = rand2D(i);
    float b = rand2D(i + vec2(1.0, 0.0));
    float c = rand2D(i + vec2(0.0, 1.0));
    float d = rand2D(i + vec2(1.0, 1.0));
    vec2 u = quintic(f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 uv, int octaves) {
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

vec3 palette(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    return a + b * cos(TAU * (c * t + d));
}

vec4 renderMinimalZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 10.0);
    float borderWidth = customParams[0].y > 0.1 ? customParams[0].y : 2.0;
    float fillOpacity = customParams[0].x > 0.1 ? customParams[0].x : 0.88;

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec3 cardColor = colorWithFallback(customColors[0].rgb, fillColor.rgb);
    cardColor = colorWithFallback(cardColor, vec3(0.12, 0.14, 0.18));
    vec3 accentColor = colorWithFallback(customColors[1].rgb, vec3(0.45, 0.55, 0.95));
    vec3 borderClr = colorWithFallback(borderColor.rgb, mix(cardColor, vec3(1.0), 0.25));

    if (isHighlighted) {
        cardColor = mix(cardColor, accentColor, 0.2);
        borderClr = accentColor;
        fillOpacity = min(fillOpacity + 0.08, 0.98);
    }

    vec4 result = vec4(0.0);
    if (d < 0.0) {
        result.rgb = cardColor * (1.0 + (1.0 - localUV.y) * 0.06);
        result.a = fillOpacity;
    }
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        result.rgb = mix(result.rgb, borderClr, border);
        result.a = max(result.a, border * 0.95);
    }
    if (isHighlighted && d > 0.0 && d < 14.0) {
        float glow = expGlow(d, 6.0, 0.45);
        result.rgb += accentColor * glow;
        result.a = max(result.a, glow * 0.55);
    }
    return result;
}

float labelOutlineAlpha(vec2 uv, float px, float py) {
    float center = texture(uZoneLabels, uv).a;
    float o = 0.0;
    o = max(o, texture(uZoneLabels, uv + vec2( px,  0.0)).a);
    o = max(o, texture(uZoneLabels, uv + vec2(-px,  0.0)).a);
    o = max(o, texture(uZoneLabels, uv + vec2( 0.0,  py)).a);
    o = max(o, texture(uZoneLabels, uv + vec2( 0.0, -py)).a);
    o = max(o, texture(uZoneLabels, uv + vec2( px,  py)).a);
    o = max(o, texture(uZoneLabels, uv + vec2(-px,  py)).a);
    o = max(o, texture(uZoneLabels, uv + vec2( px, -py)).a);
    o = max(o, texture(uZoneLabels, uv + vec2(-px, -py)).a);
    return smoothstep(0.12, 0.45, o) * (1.0 - smoothstep(0.0, 0.18, center));
}

vec4 compositeLabelsFlow(vec4 color, vec2 uv) {
    vec2 res = max(iResolution, vec2(0.0001));

    float flowSpeed = customParams[1].x > 0.0 ? customParams[1].x : 0.4;
    float noiseScale = customParams[1].y > 0.1 ? customParams[1].y : 8.0;
    int octaves = int(customParams[1].z > 1.0 ? customParams[1].z : 5.0);
    float contrast = customParams[1].w > 0.1 ? customParams[1].w : 0.9;

    float outlinePx = customParams[2].x > 0.0 ? customParams[2].x : 14.0;
    float alphaCutoff = customParams[2].y > 0.0 ? customParams[2].y : 0.38;

    float labelAlpha = texture(uZoneLabels, uv).a;
    if (labelAlpha < 0.001) {
        float outlineA = labelOutlineAlpha(uv, outlinePx / res.x, outlinePx / res.y);
        if (outlineA < 0.001) return color;
    }

    vec3 palA = colorWithFallback(customColors[2].rgb, vec3(0.5));
    vec3 palB = colorWithFallback(customColors[3].rgb, vec3(0.5));
    vec3 palC = colorWithFallback(customColors[4].rgb, vec3(1.0));
    vec3 palD = colorWithFallback(customColors[5].rgb, vec3(0.0, 0.12, 0.22));
    vec3 outlineColor = colorWithFallback(customColors[1].rgb, vec3(0.9, 0.85, 1.0));

    float time = iTime * flowSpeed;
    vec2 flowUV = uv * noiseScale;
    flowUV += vec2(time * 0.7, time * 0.5);
    float q = fbm(flowUV, octaves);
    float r = fbm(flowUV + q + time * 0.3, octaves);
    vec3 flowColor = palette(r * contrast, palA, palB, palC, palD);

    float inFill = smoothstep(alphaCutoff - 0.08, alphaCutoff + 0.08, labelAlpha);
    vec3 numberRgb = mix(color.rgb, flowColor, inFill);

    float outlineA = labelOutlineAlpha(uv, outlinePx / res.x, outlinePx / res.y);
    if (outlineA > 0.0) {
        numberRgb = mix(numberRgb, outlineColor, outlineA * 0.92);
    }

    float outAlpha = max(labelAlpha, outlineA * 0.9);
    color.rgb = color.rgb * (1.0 - outAlpha) + numberRgb * outAlpha;
    color.a = outAlpha + color.a * (1.0 - outAlpha);
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
        vec4 zoneColor = renderMinimalZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsFlow(color, labelsUv(fragCoord));

    fragColor = clampFragColor(color);
}
