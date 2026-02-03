// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0, std140) uniform ZoneUniforms {
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
 * ROTATING TILES - Shadertoy conversion
 * Rotating tiled grid with radial wave; tile edges pulse from center.
 * Adapted from Shadertoy mainImage(out vec4 fragColor, in vec2 fragCoord)
 *
 * Parameters:
 *   customParams[0].x = tileScale (10.0-40.0) - Number of tiles (higher = smaller tiles)
 *   customParams[0].y = speed (0.5-2.0) - Rotation and wave speed
 *   customParams[0].z = radialWaveScale (10.0-40.0) - Radial wave frequency
 *   customParams[0].w = baseAngle (degrees) - Base rotation angle
 *   customColors[0] = tint - Colorizes the effect (default warm gradient)
 */

#include <common.glsl>

// Shadertoy-derived: rotating tile grid with radial wave. Key constants: tile_dist/edge
// control tile vs gap; square_dist drives radial wave; warm gradient (pow 2.0, 1.5, 1.2) is
// hardcoded; only tint is exposed. Tune tileScale, speed, radialWaveScale, baseAngle for look.
vec4 sampleRotatingTiles(vec2 localFragCoord, vec2 zoneSize, float iTime, float tileScale, float speed, float radialWaveScale, float baseAngleDeg) {
    float aspect_ratio = zoneSize.y / zoneSize.x;
    vec2 uv = localFragCoord / zoneSize.x;
    uv -= vec2(0.5, 0.5 * aspect_ratio);

    float rot = radians(-baseAngleDeg - iTime * speed);
    mat2 rotation_matrix = mat2(cos(rot), -sin(rot), sin(rot), cos(rot));
    uv = rotation_matrix * uv;

    vec2 scaled_uv = tileScale * uv;
    vec2 tile = fract(scaled_uv);
    float tile_dist = min(min(tile.x, 1.0 - tile.x), min(tile.y, 1.0 - tile.y));
    float square_dist = length(floor(scaled_uv));

    float edge = sin(iTime * speed - square_dist * radialWaveScale);
    edge = mod(edge * edge, 1.0);

    float value = mix(tile_dist, 1.0 - tile_dist, step(1.0, edge));
    edge = pow(abs(1.0 - edge), 2.2) * 0.5;

    value = smoothstep(edge - 0.05, edge, 0.95 * value);

    value += square_dist * 0.1;
    value *= 0.6;

    vec3 rgb = vec3(pow(value, 2.0), pow(value, 1.5), pow(value, 1.2));
    return vec4(rgb, 1.0);
}

vec4 renderRotatingTilesZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 6.0);
    float borderWidth = max(params.y, 2.5);

    float tileScale = customParams[0].x > 1.0 ? customParams[0].x : 20.0;
    float speed = customParams[0].y > 0.01 ? customParams[0].y : 1.0;
    float radialWaveScale = customParams[0].z > 1.0 ? customParams[0].z : 20.0;
    float baseAngleDeg = abs(customParams[0].w) > 0.01 ? customParams[0].w : 30.0;
    float fillOpacity = customParams[1].x > 0.01 ? customParams[1].x : 0.9;

    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localFragCoord = fragCoord - rectPos;

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec3 tint = customColors[0].rgb;
    if (length(tint) < 0.01)
        tint = vec3(1.0, 1.0, 1.0);

    float effectiveBorderWidth = borderWidth;
    vec3 borderClr = length(borderColor.rgb) > 0.01 ? borderColor.rgb : tint;
    if (isHighlighted) {
        speed *= 1.2;
        tint = mix(tint, vec3(1.0), 0.25);
        effectiveBorderWidth = borderWidth * 2.0;
        borderClr = mix(borderClr, vec3(1.0), 0.4);
    }

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        vec4 raw = sampleRotatingTiles(localFragCoord, rectSize, iTime, tileScale, speed, radialWaveScale, baseAngleDeg);
        result.rgb = raw.rgb * tint;
        result.a = fillOpacity;
        if (isHighlighted) {
            vec3 highlightTint = length(fillColor.rgb) > 0.01 ? fillColor.rgb : vec3(0.3, 0.5, 0.9);
            result.rgb = mix(result.rgb, highlightTint, 0.35);
            result.rgb = mix(result.rgb, vec3(1.0), 0.15);
            result.a = min(result.a + 0.1, 1.0);
        }
    }

    float borderDist = abs(d);
    if (borderDist < effectiveBorderWidth + 2.0) {
        float border = 1.0 - smoothstep(0.0, effectiveBorderWidth, borderDist);
        result.rgb = mix(result.rgb, borderClr, border);
        result.a = max(result.a, border * 0.98);
    }

    if (isHighlighted && d > 0.0 && d < 45.0) {
        float glow = exp(-d / 12.0) * 0.6;
        result.rgb += tint * glow;
        result.a = max(result.a, glow * 0.65);
    }

    return result;
}

void main() {
    vec2 fragCoord = vec2(vTexCoord.x, 1.0 - vTexCoord.y) * iResolution;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0)
            continue;

        vec4 zoneColor = renderRotatingTilesZone(fragCoord, rect, zoneFillColors[i], zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);

        float srcA = zoneColor.a;
        float dstA = color.a;
        float outA = srcA + dstA * (1.0 - srcA);
        if (outA > 0.0) {
            color.rgb = (zoneColor.rgb * srcA + color.rgb * dstA * (1.0 - srcA)) / outA;
        }
        color.a = outA;
    }

    fragColor = vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
}
