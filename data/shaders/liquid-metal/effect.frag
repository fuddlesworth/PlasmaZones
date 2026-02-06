// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Liquid Metal — Final Composite Pass
// Reads the tonemapped metal surface from pass2 (iChannel2) and composites
// it into zone shapes with SDF borders, highlight effects, and labels.

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>

// Sample with slight chromatic aberration for a polished metal look
vec3 sampleMetal(vec2 uv, float chromaStr) {
    vec2 dir = (uv - vec2(0.5)) * chromaStr;
    float r = texture(iChannel2, uv + dir).r;
    float g = texture(iChannel2, uv).g;
    float b = texture(iChannel2, uv - dir).b;
    return vec3(r, g, b);
}

vec4 renderLiquidMetalZone(vec2 fragCoord, int idx) {
    vec4 rect = zoneRects[idx];
    vec4 params = zoneParams[idx];
    vec4 fillColor = zoneFillColors[idx];
    vec4 borderColor = zoneBorderColors[idx];

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;

    float borderRadius = params.x;
    float borderWidth = params.y;
    bool isHighlighted = params.z > 0.5;

    // SDF for rounded rectangle
    float d = sdRoundedBox(fragCoord - center, rectSize * 0.5, borderRadius);

    if (d > borderWidth + 20.0) return vec4(0.0); // early out with glow margin

    // Load parameters
    float fillOpacity = customParams[3].x > 0.1 ? customParams[3].x : 0.92;
    float glowIntensity = customParams[3].y;
    vec3 highlightTint = colorWithFallback(customColors[3].rgb, vec3(0.5, 0.82, 1.0));

    // Sample the rendered metal surface
    vec2 uv = fragCoord / iResolution;
    vec3 metal = sampleMetal(uv, 0.003);

    vec4 result = vec4(0.0);

    // Inside the zone
    if (d < 0.0) {
        vec3 col = metal;

        // Edge darkening for depth (liquid meniscus effect)
        float edgeFade = smoothstep(0.0, -30.0, d);
        col *= 0.85 + 0.15 * edgeFade;

        // Highlight effects
        if (isHighlighted) {
            // Tint shift toward highlight color
            col = mix(col, col * highlightTint * 1.5, 0.25);

            // Pulsing rim light
            float rim = smoothstep(-2.0, -12.0, d);
            float pulse = 0.7 + 0.3 * sin(iTime * 3.0);
            col += highlightTint * (1.0 - rim) * pulse * 0.3;

            // Inner caustic brightening
            float caustic = texture(iChannel0, uv).b;
            col += highlightTint * caustic * 0.2;
        }

        result = vec4(col, fillOpacity);
    }

    // Border: thin metallic edge
    if (borderWidth > 0.0) {
        float border = softBorder(d, borderWidth);
        if (border > 0.0) {
            vec3 bClr = colorWithFallback(borderColor.rgb, vec3(0.9, 0.92, 0.95));

            // Animated specular shimmer on border
            float angle = atan(fragCoord.y - center.y, fragCoord.x - center.x);
            float shimmer = pow(sin(angle * 3.0 + iTime * 2.0) * 0.5 + 0.5, 4.0);
            bClr += vec3(shimmer * 0.3);

            if (isHighlighted) {
                bClr = mix(bClr, highlightTint, 0.4);
                bClr += vec3(shimmer * 0.2);
            }

            vec4 borderResult = vec4(bClr, border);
            result = blendOver(result, borderResult);
        }
    }

    // Outer glow (highlight only)
    if (isHighlighted && d > 0.0 && glowIntensity > 0.0) {
        float glow = expGlow(d, 15.0, glowIntensity * 0.6);
        vec4 glowResult = vec4(highlightTint, glow);
        result = blendOver(result, glowResult);
    }

    return result;
}

void main() {
    vec2 fragCoord = fragCoordFromTexCoord(vTexCoord);
    vec4 color = vec4(0.0);

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 zoneColor = renderLiquidMetalZone(fragCoord, i);
        if (zoneColor.a > 0.001) {
            color = blendOver(color, zoneColor);
        }
    }

    // Composite zone labels
    color = compositeLabelsWithUv(color, fragCoord);

    fragColor = clampFragColor(color);
}
