// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Voronoi Stained Glass — Final composite
// Pass 0: 3D raymarched glass  -> iChannel0
// Pass 1: Horizontal bloom      -> iChannel1
// Pass 2: Bloom + tone mapping  -> iChannel2
// This pass: zone masking, highlight effects, label compositing.

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>

float getFillOpacity()   { return customParams[3].x > 0.1 ? customParams[3].x : 0.92; }
float getGlowIntensity() { return customParams[3].y; }
float getSaturation()    { return customParams[3].z > 0.1 ? customParams[3].z : 1.2; }

float luminance(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// Zone number rendered as a lead inscription with warm backlight halo.
vec4 compositeGlassLabels(vec4 color, vec2 fragCoord) {
    vec2 uv = labelsUv(fragCoord);
    vec2 px = 1.0 / max(iResolution, vec2(1.0));

    vec3 leadClr  = colorWithFallback(customColors[4].rgb, vec3(0.102, 0.102, 0.118));
    vec3 lightCol = colorWithFallback(customColors[5].rgb, vec3(1.0, 0.961, 0.878));

    vec4 labels = texture(uZoneLabels, uv);

    // Dilated mask: thin lead outline ring around the number
    float dilated = labels.a;
    float r = 2.5;
    dilated = max(dilated, texture(uZoneLabels, uv + vec2(-r, 0.0) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2( r, 0.0) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2(0.0, -r) * px).a);
    dilated = max(dilated, texture(uZoneLabels, uv + vec2(0.0,  r) * px).a);
    float outlineRing = max(0.0, dilated - labels.a);

    // Warm backglow behind the medallion
    float glow = 0.0;
    float g = 6.0;
    glow = max(glow, texture(uZoneLabels, uv + vec2(-g,  0.0) * px).a);
    glow = max(glow, texture(uZoneLabels, uv + vec2( g,  0.0) * px).a);
    glow = max(glow, texture(uZoneLabels, uv + vec2(0.0, -g)  * px).a);
    glow = max(glow, texture(uZoneLabels, uv + vec2(0.0,  g)  * px).a);
    glow = max(glow, texture(uZoneLabels, uv + vec2(-g, -g) * px * 0.707).a);
    glow = max(glow, texture(uZoneLabels, uv + vec2( g, -g) * px * 0.707).a);
    glow = max(glow, texture(uZoneLabels, uv + vec2(-g,  g) * px * 0.707).a);
    glow = max(glow, texture(uZoneLabels, uv + vec2( g,  g) * px * 0.707).a);
    float backlightMask = max(0.0, glow - dilated) * 0.35;

    color.rgb += lightCol * backlightMask;
    color.a = max(color.a, backlightMask * 0.5);

    // Lead outline ring
    color.rgb = mix(color.rgb, leadClr, outlineRing * 0.85);
    color.a = max(color.a, outlineRing * 0.9);

    // Number body: raised lead came
    if (labels.a > 0.01) {
        vec3 leadNum = leadClr * 1.15 + lightCol * 0.03;
        color.rgb = color.rgb * (1.0 - labels.a) + leadNum * labels.a;
        color.a = max(color.a, labels.a);
    }

    return color;
}

vec4 renderGlassZone(vec2 fragCoord, vec4 rect, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.0);
    float fillOpacity  = getFillOpacity();
    float sat          = getSaturation();

    vec3 leadClr  = colorWithFallback(customColors[4].rgb, vec3(0.102, 0.102, 0.118));
    vec3 lightCol = colorWithFallback(customColors[5].rgb, vec3(1.0, 0.961, 0.878));

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Fixed-width AA (sdRoundedBox is in pixel units)
    float fill = 1.0 - smoothstep(-0.5, 0.5, d);

    vec4 result = vec4(0.0);

    if (fill > 0.001) {
        // Sample post-processed 3D glass from pass 2
        vec3 glass = texture(iChannel2, channelUv(2, fragCoord)).rgb;

        // Saturation adjustment
        float lum = luminance(glass);
        glass = mix(vec3(lum), glass, sat);

        // Subtle bevel: darken near zone edge for inset glass look
        float bevelWidth = max(borderWidth * 3.0, 8.0);
        float bevelDist  = smoothstep(0.0, bevelWidth, abs(d));
        glass *= mix(0.75, 1.0, bevelDist);
        // Bevel highlight ridge
        float bevelHL = (1.0 - bevelDist) * bevelDist * 4.0 * 0.08;
        glass += lightCol * bevelHL;

        if (isHighlighted) {
            // Sunbeam sweeping across the glass
            float beamAngle = iTime * 0.4;
            vec2 beamDir = vec2(cos(beamAngle), sin(beamAngle));
            float beamPos = dot(localUV - 0.5, beamDir);
            float beam = exp(-beamPos * beamPos * 18.0) * 0.3;

            // Prismatic edge refraction at bevel
            float prismPhase = dot(fragCoord, vec2(0.037, 0.023)) + iTime * 0.6;
            vec3 prism = vec3(
                sin(prismPhase) * 0.5 + 0.5,
                sin(prismPhase + 2.094) * 0.5 + 0.5,
                sin(prismPhase + 4.189) * 0.5 + 0.5
            );
            vec3 prismContrib = prism * (1.0 - bevelDist) * 0.08;

            // Radial inner glow
            float radial = 1.0 - length(localUV - 0.5) * 1.8;
            float pulse = 0.7 + 0.3 * sin(iTime * 1.2);
            float innerGlow = max(0.0, radial) * pulse * 0.12;

            result.rgb = glass + lightCol * (beam + innerGlow) + prismContrib;
            result.a   = fillOpacity * fill;
        } else {
            result.rgb = glass * 0.92;
            result.a   = fillOpacity * fill;
        }

        result.rgb = clamp(result.rgb, 0.0, 1.0);
    }

    // Zone border drawn with lead color
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        float borderStr = isHighlighted ? 0.95 : 0.85;
        result.rgb = mix(result.rgb, leadClr, border * borderStr);
        result.a   = max(result.a, border * 0.95);

        if (isHighlighted) {
            float specAngle = iTime * 0.3;
            vec2 specDir = vec2(cos(specAngle), sin(specAngle));
            float pLen = length(p);
            float spec = pLen > 0.5
                ? pow(max(0.0, dot(p / pLen, specDir)), 12.0)
                : 0.0;
            result.rgb += lightCol * spec * border * 0.2;
        }
    }

    // Outer glow for highlighted zones
    if (isHighlighted && d > 0.0 && d < 25.0) {
        float glow = expGlow(d, 8.0, getGlowIntensity());
        vec3 glowCol = mix(lightCol, lightCol * vec3(1.0, 0.85, 0.6), d / 25.0);
        result.rgb += glowCol * glow * 0.5;
        result.a    = max(result.a, glow * 0.6);
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
        vec4 zoneColor = renderGlassZone(fragCoord, rect,
            zoneParams[i], isHighlighted);
        color = blendOver(color, zoneColor);
    }

    color = compositeGlassLabels(color, fragCoord);
    fragColor = clampFragColor(color);
}
