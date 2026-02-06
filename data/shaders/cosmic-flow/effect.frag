// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

/*
 * COSMIC FLOW - Fragment Shader
 * 
 * Flowing fractal brownian motion noise with animated color palette.
 * Creates mesmerizing, organic flowing patterns inside zones.
 * Based on Inigo Quilez's palette technique and classic fbm.
 */

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>

// Pseudo-random 2D hash
float rand2D(in vec2 p) {
    return fract(sin(dot(p, vec2(15.285, 97.258))) * 47582.122);
}

// Quintic interpolation for C2 continuity; eliminates visible cell boundaries
vec2 quintic(vec2 f) {
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

// Value noise
float noise(in vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    
    float a = rand2D(i);
    float b = rand2D(i + vec2(1.0, 0.0));
    float c = rand2D(i + vec2(0.0, 1.0));
    float d = rand2D(i + vec2(1.0, 1.0));

    vec2 u = quintic(f);
    float lower = mix(a, b, u.x);
    float upper = mix(c, d, u.x);
    
    return mix(lower, upper, u.y);
}

// Fractal Brownian Motion with rotation
float fbm(in vec2 uv, int octaves) {
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

// Inigo Quilez palette function
vec3 palette(in float t, in vec3 a, in vec3 b, in vec3 c, in vec3 d) {
    return a + b * cos(TAU * (c * t + d));
}

vec4 renderCosmicZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);
    
    // Get shader parameters with defaults
    float speed = customParams[0].x > 0.001 ? customParams[0].x : 0.1;
    float flowSpeed = customParams[0].y > 0.001 ? customParams[0].y : 0.3;
    float noiseScale = customParams[0].z > 0.1 ? customParams[0].z : 3.0;
    int octaves = int(customParams[0].w > 1.0 ? customParams[0].w : 6.0);
    
    float colorShift = customParams[1].x;
    float saturation = customParams[1].y > 0.01 ? customParams[1].y : 0.5;
    float brightness = customParams[1].z > 0.01 ? customParams[1].z : 0.5;
    float contrast = customParams[1].w > 0.1 ? customParams[1].w : 0.95;
    
    float fillOpacity = customParams[2].x > 0.1 ? customParams[2].x : 0.85;
    float borderGlow = customParams[2].y;
    float edgeFadeStart = customParams[2].z < 0.0 ? customParams[2].z : -30.0;
    float borderBrightness = customParams[2].w > 0.1 ? customParams[2].w : 1.3;
    
    // Convert rect to pixel coordinates
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;
    
    // Position relative to zone center
    vec2 p = fragCoord - center;
    
    // Calculate SDF
    float d = sdRoundedBox(p, halfSize, borderRadius);
    
    // UV for the cosmic effect - normalized and centered
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    vec2 centeredUV = (localUV * 2.0 - 1.0) * noiseScale;
    
    // Aspect correction
    float aspect = rectSize.x / rectSize.y;
    centeredUV.x *= aspect;
    
    // Palette colors from uniforms or defaults
    vec3 palA = customColors[0].rgb;
    vec3 palB = customColors[1].rgb;
    vec3 palC = customColors[2].rgb;
    vec3 palD = customColors[3].rgb;
    
    // IQ palette: when palette colors are unset, palA/palB become gray (brightness/saturation);
    // set palette colors in the UI for full control.
    palA = colorWithFallback(palA, vec3(brightness));
    palB = colorWithFallback(palB, vec3(saturation));
    palC = colorWithFallback(palC, vec3(1.0));
    palD = colorWithFallback(palD, vec3(0.0, 0.10, 0.20));
    
    // Apply color shift to phase
    palD += vec3(colorShift);
    
    vec4 result = vec4(0.0);
    float time = iTime;

    // Inside the zone
    if (d < 0.0) {
        
        // First fbm layer - slow drift
        float q = fbm(centeredUV + time * speed, octaves);
        
        // Second fbm layer - combines with first for complexity
        float r = fbm(centeredUV + q + time * flowSpeed, octaves);
        
        // Generate color from palette
        vec3 col = palette(r * contrast, palA, palB, palC, palD);
        
        // Highlight effect - brighter and more saturated
        if (isHighlighted) {
            col = mix(col, vec3(1.0), 0.15);
            col *= 1.2;
        }
        
        // Edge darkening for depth
        float edgeFade = smoothstep(0.0, edgeFadeStart, d);
        col *= mix(0.7, 1.0, edgeFade);
        
        result.rgb = col;
        result.a = fillOpacity;
    }
    
    // Border
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        
        // Animated border using the same palette
        float angle = atan(p.y, p.x);
        float borderFlow = fbm(vec2(angle * 2.0, time * 0.5), 3);
        vec3 borderCol = palette(borderFlow * contrast, palA, palB, palC, palD);
        borderCol *= borderBrightness;
        
        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }
    
    // Outer glow
    if (d > 0.0 && d < 20.0 && borderGlow > 0.01) {
        float glow = expGlow(d, 8.0, borderGlow);
        
        // Glow color from palette
        float angle = atan(p.y, p.x);
        vec3 glowCol = palette(angle / TAU + time * 0.1, palA, palB, palC, palD);
        
        result.rgb += glowCol * glow * 0.5;
        result.a = max(result.a, glow * 0.4);
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
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        
        vec4 zoneColor = renderCosmicZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);

    fragColor = clampFragColor(color);
}
