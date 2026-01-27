// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 330 core

/*
 * COSMIC FLOW - Fragment Shader
 * 
 * Flowing fractal brownian motion noise with animated color palette.
 * Creates mesmerizing, organic flowing patterns inside zones.
 * Based on Inigo Quilez's palette technique and classic fbm.
 */

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
    vec4 iMouse;
    // customParams[0]: speed, flowSpeed, noiseScale, octaves
    // customParams[1]: colorShift, saturation, brightness, contrast
    // customParams[2]: fillOpacity, borderGlow, unused, unused
    vec4 customParams[4];
    // customColors[0-3]: palette colors (a, b, c, d)
    vec4 customColors[8];
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

const float PI = 3.14159265359;
const float TAU = 6.28318530718;

// Pseudo-random 2D hash
float rand2D(in vec2 p) {
    return fract(sin(dot(p, vec2(15.285, 97.258))) * 47582.122);
}

// Value noise
float noise(in vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    
    float a = rand2D(i);
    float b = rand2D(i + vec2(1.0, 0.0));
    float c = rand2D(i + vec2(0.0, 1.0));
    float d = rand2D(i + vec2(1.0, 1.0));

    vec2 u = smoothstep(0.0, 1.0, f);
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

// Rounded box SDF
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
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
    
    // Convert rect to pixel coordinates
    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;
    
    // Position relative to zone center
    vec2 p = fragCoord - center;
    
    // Calculate SDF
    float d = sdRoundedBox(p, halfSize, borderRadius);
    
    // UV for the cosmic effect - normalized and centered
    vec2 localUV = (fragCoord - rectPos) / rectSize;
    vec2 centeredUV = (localUV * 2.0 - 1.0) * noiseScale;
    
    // Aspect correction
    float aspect = rectSize.x / rectSize.y;
    centeredUV.x *= aspect;
    
    // Palette colors from uniforms or defaults
    vec3 palA = customColors[0].rgb;
    vec3 palB = customColors[1].rgb;
    vec3 palC = customColors[2].rgb;
    vec3 palD = customColors[3].rgb;
    
    // Use defaults if colors not set
    if (length(palA) < 0.01) palA = vec3(brightness);
    if (length(palB) < 0.01) palB = vec3(saturation);
    if (length(palC) < 0.01) palC = vec3(1.0);
    if (length(palD) < 0.01) palD = vec3(0.0, 0.10, 0.20);
    
    // Apply color shift to phase
    palD += vec3(colorShift);
    
    vec4 result = vec4(0.0);
    
    // Inside the zone
    if (d < 0.0) {
        float time = iTime;
        
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
        float edgeFade = smoothstep(0.0, -30.0, d);
        col *= mix(0.7, 1.0, edgeFade);
        
        result.rgb = col;
        result.a = fillOpacity;
    }
    
    // Border
    float borderDist = abs(d);
    if (borderDist < borderWidth + 2.0) {
        float border = 1.0 - smoothstep(0.0, borderWidth, borderDist);
        
        // Animated border using the same palette
        float time = iTime;
        float angle = atan(p.y, p.x);
        float borderFlow = fbm(vec2(angle * 2.0, time * 0.5), 3);
        vec3 borderCol = palette(borderFlow * contrast, palA, palB, palC, palD);
        borderCol *= 1.3; // Brighter border
        
        result.rgb = mix(result.rgb, borderCol, border * 0.95);
        result.a = max(result.a, border * 0.98);
    }
    
    // Outer glow
    if (d > 0.0 && d < 20.0 && borderGlow > 0.01) {
        float glow = exp(-d / 8.0) * borderGlow;
        
        // Glow color from palette
        float time = iTime;
        float angle = atan(p.y, p.x);
        vec3 glowCol = palette(angle / TAU + time * 0.1, palA, palB, palC, palD);
        
        result.rgb += glowCol * glow * 0.5;
        result.a = max(result.a, glow * 0.4);
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
        
        vec4 zoneColor = renderCosmicZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        
        // Alpha compositing
        color.rgb = mix(color.rgb, zoneColor.rgb, zoneColor.a);
        color.a = max(color.a, zoneColor.a);
    }
    
    fragColor = vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
}
