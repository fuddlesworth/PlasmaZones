// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

/*
 * MINIMALIST - Clean Modern Card Style
 * 
 * Flat design with depth cues: soft shadows, 
 * clean borders, subtle gradients, and smooth highlights.
 * 
 * Parameters:
 *   customParams[0].x = fillOpacity (0.4-0.95) - Card opacity
 *   customParams[0].y = borderWidth (1.0-4.0) - Border thickness
 *   customParams[0].z = shadowSize (5.0-30.0) - Shadow spread
 *   customParams[0].w = shadowOpacity (0.1-0.5) - Shadow darkness
 *   customParams[1].x = innerGlow (0.0-0.5) - Inner edge brightness
 *   customParams[1].y = bevelStrength (0.0-0.3) - 3D bevel effect
 */

#include <common.glsl>

vec4 renderMinimalistZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    // Zone shape: params.x = borderRadius, params.y = zone-level border (unused; we use shader param)
    float borderRadius = max(params.x, 10.0);
    
    float fillOpacity = customParams[0].x > 0.1 ? customParams[0].x : 0.85;
    float borderWidth = customParams[0].y > 0.1 ? customParams[0].y : 2.0;
    float shadowSize = customParams[0].z > 1.0 ? customParams[0].z : 15.0;
    float shadowOpacity = customParams[0].w > 0.01 ? customParams[0].w : 0.3;
    float innerGlow = customParams[1].x > 0.001 ? customParams[1].x : 0.15;
    float bevelStrength = customParams[1].y > 0.001 ? customParams[1].y : 0.12;
    
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    // Colors
    vec3 cardColor = colorWithFallback(customColors[0].rgb, fillColor.rgb);
    cardColor = colorWithFallback(cardColor, vec3(0.18, 0.2, 0.25));
    vec3 accentColor = colorWithFallback(customColors[1].rgb, vec3(0.3, 0.55, 0.95));
    vec3 borderClr = colorWithFallback(borderColor.rgb, mix(cardColor, vec3(1.0), 0.3));
    
    if (isHighlighted) {
        cardColor = mix(cardColor, accentColor, 0.25);
        borderClr = accentColor;
        fillOpacity = min(fillOpacity + 0.1, 0.98);
        shadowOpacity *= 1.3;
        shadowSize *= 1.2;
    }
    
    vec4 result = vec4(0.0);
    
    // Multi-layer soft shadow
    if (d > 0.0 && d < shadowSize * 2.0) {
        vec2 shadowOffset = vec2(3.0, 5.0);
        float shadowD = sdRoundedBox(p - shadowOffset, rectSize * 0.5, borderRadius);
        
        if (shadowD > 0.0) {
            // Layered shadow for softness
            float shadow1 = exp(-shadowD / (shadowSize * 0.4)) * shadowOpacity;
            float shadow2 = exp(-shadowD / (shadowSize * 0.8)) * shadowOpacity * 0.5;
            float shadow3 = exp(-shadowD / (shadowSize * 1.5)) * shadowOpacity * 0.25;
            float totalShadow = shadow1 + shadow2 + shadow3;
            
            result.rgb = vec3(0.0);
            result.a = totalShadow;
        }
    }
    
    // Main card fill
    if (d < 0.0) {
        vec3 fill = cardColor;
        
        // Subtle vertical gradient (lighter at top)
        float gradient = 1.0 + (1.0 - localUV.y) * 0.08;
        fill *= gradient;
        
        // Bevel effect - top/left lighter, bottom/right darker
        float bevelTop = smoothstep(0.0, 15.0, -d) * bevelStrength;
        float topLight = (1.0 - localUV.y) * bevelTop;
        float leftLight = (1.0 - localUV.x) * bevelTop * 0.5;
        fill += vec3(topLight + leftLight);
        
        float bottomDark = localUV.y * bevelTop * 0.5;
        float rightDark = localUV.x * bevelTop * 0.3;
        fill -= vec3(bottomDark + rightDark);
        
        // Inner glow near edges
        float edgeDist = -d;
        if (edgeDist < 20.0) {
            float glow = (1.0 - edgeDist / 20.0) * innerGlow;
            fill += vec3(glow);
        }
        
        result.rgb = fill;
        result.a = fillOpacity;
    }
    
    // Clean border
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        // Border with slight gradient
        vec3 bColor = borderClr;
        bColor *= 0.9 + localUV.y * 0.2; // Lighter at bottom
        
        result.rgb = mix(result.rgb, bColor, border);
        result.a = max(result.a, border * 0.95);
    }
    
    // Top highlight line (like light catching edge)
    if (d < 0.0 && d > -borderWidth * 1.5) {
        float topEdge = 1.0 - localUV.y;
        if (topEdge > 0.995) {
            result.rgb = mix(result.rgb, vec3(1.0), 0.4);
        }
    }
    
    // Highlight outer glow
    if (isHighlighted && d > 0.0 && d < 15.0) {
        float glow = expGlow(d, 6.0, 0.5);
        result.rgb += accentColor * glow;
        result.a = max(result.a, glow * 0.6);
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
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        
        vec4 zoneColor = renderMinimalistZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);
    
    fragColor = clampFragColor(color);
}
