// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Nexus Cascade — Multi-pass multi-channel zone overlay
// Pass 0: plasma base → iChannel0
// Pass 1: distorted + scanline layer → iChannel1
// Pass 2: bloom combine → iChannel2
// This pass: zone mask, chromatic blend, labels.

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>

float getChromaStrength() { return customParams[3].x > 0.0 ? customParams[3].x : 4.0; }
float getFillOpacity()    { return customParams[3].y > 0.01 ? customParams[3].y : 0.92; }
float getChannelMix()     { return customParams[3].z > 0.0 ? customParams[3].z : 0.5; }
float getZoneFillTint()  { return customParams[3].w; }

float luminance(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec4 sampleNexus(vec2 fragCoord, vec2 uv, float chroma) {
    vec4 c0 = texture(iChannel0, channelUv(0, fragCoord));
    vec4 c1 = texture(iChannel1, channelUv(1, fragCoord));
    vec4 c2 = texture(iChannel2, channelUv(2, fragCoord));

    if (chroma > 0.5) {
        vec2 rOffPx = vec2(chroma, 0.0);
        vec2 bOffPx = vec2(-chroma, 0.0);
        float r = texture(iChannel2, channelUv(2, fragCoord + rOffPx)).r;
        float g = c2.g;
        float b = texture(iChannel2, channelUv(2, fragCoord + bOffPx)).b;
        c2 = vec4(r, g, b, 1.0);
    }

    float mixVal = getChannelMix();
    vec3 col = mix(c2.rgb, mix(c0.rgb, c1.rgb, 0.5), mixVal * 0.5);
    return vec4(clamp(col, 0.0, 1.0), 1.0);
}

vec4 renderNexusZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);
    float fillOpacity = getFillOpacity();
    float chroma = getChromaStrength();

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec4 result = vec4(0.0);

    vec3 borderClr = colorWithFallback(borderColor.rgb, vec3(0.5, 0.6, 1.0));

    if (d < 0.0) {
        vec4 nexus = sampleNexus(fragCoord, localUV, isHighlighted ? chroma * 1.5 : chroma);
        vec3 zoneFill = fillColor.rgb;
        float tintAmount = getZoneFillTint();
        result.rgb = mix(nexus.rgb, nexus.rgb * zoneFill, tintAmount);
        result.a = fillOpacity;

        if (isHighlighted) {
            result.a = min(result.a + 0.06, 1.0);
            float sat = 1.25;
            float lum = luminance(result.rgb);
            result.rgb = mix(vec3(lum), result.rgb, sat);
            float pulse = 0.5 + 0.5 * sin(iTime * 2.2);
            float innerGlow = (1.0 - length(localUV - 0.5) * 2.0) * pulse * 0.18;
            innerGlow = max(0.0, innerGlow);
            result.rgb += borderClr * innerGlow;
        } else {
            float desat = 0.5;
            float lum = luminance(result.rgb);
            result.rgb = mix(vec3(lum), result.rgb, 1.0 - desat);
            result.rgb *= 0.88;
        }
        result.rgb = clamp(result.rgb, 0.0, 1.0);
    }

    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        result.rgb = mix(result.rgb, borderClr, border * (isHighlighted ? 0.95 : 0.85));
        result.a = max(result.a, border * 0.98);
    }

    if (isHighlighted && d > 0.0 && d < 22.0) {
        float glow = expGlow(d, 7.0, 0.5);
        result.rgb += borderClr * glow;
        result.a = max(result.a, glow * 0.65);
    }

    return result;
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
        if (rect.z <= 0.0 || rect.w <= 0.0)
            continue;
        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderNexusZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);
    fragColor = clampFragColor(color);
}
