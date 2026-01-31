// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 330 core

in vec2 vTexCoord;
out vec4 fragColor;

layout(std140) uniform ZoneUniforms {
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
 * NEON GLOW - Cyberpunk Neon Border Effect
 * 
 * Parameters:
 *   customParams[0].x = glowIntensity (1.0-4.0) - Overall glow brightness
 *   customParams[0].y = pulseSpeed (0.5-4.0) - Pulse animation speed
 *   customParams[0].z = glowSize (10.0-60.0) - Glow spread distance
 *   customParams[0].w = coreIntensity (1.0-3.0) - Border core brightness
 *   customParams[1].x = fillDim (0.0-0.3) - Inner fill darkness
 *   customParams[1].y = flickerAmount (0.0-0.3) - Random flicker
 */

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float hash(float n) {
    return fract(sin(n) * 43758.5453123);
}

vec4 renderNeonZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 6.0);
    float borderWidth = max(params.y, 3.0);
    
    float glowIntensity = customParams[0].x > 0.1 ? customParams[0].x : 2.5;
    float pulseSpeed = customParams[0].y > 0.1 ? customParams[0].y : 2.0;
    float glowSize = customParams[0].z > 1.0 ? customParams[0].z : 35.0;
    float coreIntensity = customParams[0].w > 0.1 ? customParams[0].w : 2.0;
    float fillDim = customParams[1].x > 0.001 ? customParams[1].x : 0.08;
    float flickerAmount = customParams[1].y > 0.001 ? customParams[1].y : 0.1;
    float cornerGlowStrength = customParams[1].z > 0.001 ? customParams[1].z : 0.5;
    float innerGlowSpread = customParams[1].w > 0.01 ? customParams[1].w : 0.25;
    
    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    // Neon colors
    vec3 neonColor = customColors[0].rgb;
    if (length(neonColor) < 0.01) neonColor = fillColor.rgb;
    if (length(neonColor) < 0.01) neonColor = vec3(1.0, 0.0, 0.4); // #ff0066 hot pink
    
    vec3 accentColor = customColors[1].rgb;
    if (length(accentColor) < 0.01) accentColor = vec3(0.0, 1.0, 1.0); // #00ffff cyan
    
    if (isHighlighted) {
        neonColor = accentColor;
        glowIntensity *= 1.4;
        glowSize *= 1.3;
    }
    
    // Pulse animation
    float pulse = 0.85 + 0.15 * sin(iTime * pulseSpeed * 3.14159);
    
    // Random flicker
    float flicker = 1.0 - flickerAmount * hash(floor(iTime * 30.0));
    
    float intensity = pulse * flicker;
    
    vec4 result = vec4(0.0);
    
    float borderDist = abs(d);
    
    // Inside the zone - inner glow effect
    if (d < 0.0) {
        // Dark base
        result.rgb = neonColor * fillDim * intensity;
        result.a = 0.85;
        
        // Primary inner glow (strongest near border)
        float innerDist = -d; // Distance from border going inward
        float glow1 = exp(-innerDist / (glowSize * innerGlowSpread)) * glowIntensity * 0.9;
        // Secondary softer inner glow
        float glow2 = exp(-innerDist / (glowSize * innerGlowSpread * 2.0)) * glowIntensity * 0.5;
        // Tertiary ambient inner glow
        float glow3 = exp(-innerDist / (glowSize * innerGlowSpread * 4.0)) * glowIntensity * 0.25;
        
        float totalInnerGlow = (glow1 + glow2 + glow3) * intensity;
        result.rgb += neonColor * totalInnerGlow;
        result.a = max(result.a, min(totalInnerGlow * 0.9 + 0.3, 1.0));
    }
    
    // Bright neon core (the actual border line)
    float coreWidth = borderWidth * 0.6;
    float core = 1.0 - smoothstep(0.0, coreWidth, borderDist);
    if (core > 0.0) {
        vec3 coreColor = neonColor * coreIntensity * intensity;
        // White hot center
        coreColor = mix(coreColor, vec3(1.0), core * 0.7);
        result.rgb = max(result.rgb, coreColor * core);
        result.a = max(result.a, core);
    }
    
    // Subtle outer glow (just a thin halo, not the main effect)
    if (d > 0.0 && d < glowSize * 0.3) {
        float outerGlow = exp(-d / (glowSize * 0.1)) * glowIntensity * 0.3;
        result.rgb += neonColor * outerGlow * intensity;
        result.a = max(result.a, outerGlow * 0.5);
    }
    
    // Corner accents inside
    if (d < 0.0) {
        vec2 localUV = (fragCoord - rectPos) / rectSize;
        vec2 cornerDist = abs(localUV - 0.5) * 2.0;
        float cornerProximity = max(cornerDist.x, cornerDist.y);
        if (cornerProximity > 0.85) {
            float cornerGlow = (cornerProximity - 0.85) / 0.15 * cornerGlowStrength * intensity;
            result.rgb += neonColor * cornerGlow;
        }
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
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        
        vec4 zoneColor = renderNeonZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        
        // Additive blend for glow
        color.rgb += zoneColor.rgb * zoneColor.a;
        color.a = max(color.a, zoneColor.a);
    }
    
    fragColor = vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
}
