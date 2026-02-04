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
 * RIPPLE LABELS - Liquid surface with expanding wavefronts; zone numbers refracted by the same wave
 *
 * Zone: Dark pool with concentric ripples emanating from the zone center (distance-based wave).
 * Number: Sampled through a refraction offset driven by the same wave so digits bend with the surface.
 * Single physical idea: a wavy surface; the zone shows the waves, the label is seen through it.
 *
 * Parameters:
 *   customParams[0].x = fillOpacity, .y = borderWidth, .z = rippleFreq (rings per unit dist), .w = rippleSpeed
 *   customParams[1].x = ringSharpness (line sharpness), .y = refractStrength (UV displacement), .z = ringBrightness
 */

#include <common.glsl>

// Ripple phase from zone center (local 0-1 space); distScale makes dist ~1 at edge of zone
float ripplePhase(vec2 localUV, float freq, float speed) {
    vec2 toCenter = localUV - 0.5;
    float dist = length(toCenter) * 2.0;
    return dist * freq - iTime * speed;
}

// Ring intensity: bright at wave crests
float ringAtCrest(float phase, float sharpness) {
    float s = sin(phase);
    return pow(smoothstep(1.0 - sharpness, 1.0, s), 2.0);
}

vec4 renderRippleZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);

    float fillOpacity = customParams[0].x > 0.01 ? customParams[0].x : 0.82;
    float rippleFreq = customParams[0].z > 0.1 ? customParams[0].z : 12.0;
    float rippleSpeed = customParams[0].w > 0.01 ? customParams[0].w : 2.0;
    float ringSharpness = customParams[1].x > 0.01 ? customParams[1].x : 0.08;
    float ringBrightness = customParams[1].z > 0.01 ? customParams[1].z : 0.7;

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    localUV = clamp(localUV, 0.0, 1.0);

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec3 poolColor = colorWithFallback(customColors[0].rgb, vec3(0.06, 0.08, 0.12));
    vec3 ringColor = colorWithFallback(customColors[1].rgb, vec3(0.35, 0.65, 0.9));

    if (isHighlighted) {
        ringColor = mix(ringColor, vec3(1.0), 0.35);
        rippleSpeed *= 1.2;
        ringBrightness = min(ringBrightness + 0.2, 1.0);
    }

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        float phase = ripplePhase(localUV, rippleFreq, rippleSpeed);
        float ring = ringAtCrest(phase, ringSharpness);

        result.rgb = poolColor + ringColor * (ring * ringBrightness);
        result.a = fillOpacity;
    }

    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        result.rgb = mix(result.rgb, ringColor, border * 0.9);
        result.a = max(result.a, border * 0.95);
    }

    if (isHighlighted && d > 0.0 && d < 16.0) {
        float glow = expGlow(d, 6.0, 0.35);
        result.rgb += ringColor * glow;
        result.a = max(result.a, glow * 0.5);
    }

    return result;
}

// Refract labels by a global ripple (same wave language as zone; center at overlay center)
vec4 compositeLabelsRipple(vec4 color, vec2 uv) {
    float refractStrength = customParams[1].y > 0.0 ? customParams[1].y : 0.015;
    float rippleFreq = customParams[0].z > 0.1 ? customParams[0].z : 12.0;
    float rippleSpeed = customParams[0].w > 0.01 ? customParams[0].w : 2.0;

    vec2 toCenter = uv - 0.5;
    float dist = length(toCenter) * 2.0;
    float phase = dist * rippleFreq - iTime * rippleSpeed;
    float wave = sin(phase);

    vec2 dir = length(toCenter) > 0.001 ? normalize(toCenter) : vec2(0.0, 1.0);
    vec2 displacement = dir * wave * refractStrength;
    vec2 uvRefract = uv + displacement;
    uvRefract = clamp(uvRefract, vec2(0.0), vec2(1.0));

    vec4 labels = texture(uZoneLabels, uvRefract);
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

        vec4 zoneColor = renderRippleZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsRipple(color, labelsUv(fragCoord));

    fragColor = clampFragColor(color);
}
