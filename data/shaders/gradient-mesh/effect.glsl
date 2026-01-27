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
 * GRADIENT MESH - Flowing Color Gradient Effect
 * 
 * Smooth animated gradient with organic flowing colors.
 * 
 * Parameters:
 *   customParams[0].x = animationSpeed (0.2-2.0) - Flow animation speed
 *   customParams[0].y = colorIntensity (0.5-1.5) - Color vibrancy
 *   customParams[0].z = waveScale (1.0-5.0) - Size of gradient waves
 *   customParams[0].w = opacity (0.4-0.9) - Overall opacity
 *   customParams[1].x = borderBlend (0.0-1.0) - Border color blend
 *   customParams[1].y = contrast (0.5-1.5) - Gradient contrast
 */

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Smooth noise
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x),
        f.y
    );
}

float fbm(vec2 p) {
    float f = 0.0;
    float w = 0.5;
    for (int i = 0; i < 4; i++) {
        f += w * noise(p);
        p *= 2.0;
        w *= 0.5;
    }
    return f;
}

vec3 gradientColor(vec2 uv, float time, vec3 color1, vec3 color2, float scale, float intensity) {
    // Flowing noise pattern
    vec2 q = uv * scale;
    q.x += time * 0.3;
    q.y += sin(time * 0.5) * 0.2;
    
    float n1 = fbm(q);
    float n2 = fbm(q + vec2(5.2, 1.3) + time * 0.2);
    float n3 = fbm(q + vec2(1.7, 9.2) - time * 0.15);
    
    // Combine noises
    float pattern = (n1 + n2 * 0.5 + n3 * 0.25) / 1.75;
    
    // Add directional gradient
    float gradient = uv.x * 0.3 + uv.y * 0.3 + sin(uv.x * 3.0 + time) * 0.1;
    pattern = mix(pattern, gradient, 0.3);
    
    // Apply contrast
    pattern = pow(pattern, 1.0 / intensity);
    
    // Color blend
    return mix(color1, color2, pattern);
}

vec4 renderGradientZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);
    
    float animationSpeed = customParams[0].x > 0.01 ? customParams[0].x : 0.8;
    float colorIntensity = customParams[0].y > 0.1 ? customParams[0].y : 1.0;
    float waveScale = customParams[0].z > 0.1 ? customParams[0].z : 2.5;
    float opacity = customParams[0].w > 0.1 ? customParams[0].w : 0.7;
    float borderBlend = customParams[1].x > 0.01 ? customParams[1].x : 0.5;
    float contrast = customParams[1].y > 0.1 ? customParams[1].y : 1.0;
    
    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = (fragCoord - rectPos) / rectSize;
    
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    // Gradient colors
    vec3 color1 = customColors[0].rgb;
    if (length(color1) < 0.01) color1 = fillColor.rgb;
    if (length(color1) < 0.01) color1 = vec3(0.976, 0.451, 0.086);  // #f97316 orange
    
    vec3 color2 = customColors[1].rgb;
    if (length(color2) < 0.01) color2 = vec3(0.486, 0.227, 0.929);  // #7c3aed violet
    
    if (isHighlighted) {
        color1 = mix(color1, vec3(1.0), 0.2);
        color2 = mix(color2, vec3(1.0), 0.2);
        opacity += 0.1;
    }
    
    float time = iTime * animationSpeed;
    
    vec4 result = vec4(0.0);
    
    if (d < 0.0) {
        // Main gradient
        vec3 gradient = gradientColor(localUV, time, color1, color2, waveScale, contrast);
        
        // Add shimmer
        float shimmer = sin(localUV.x * 20.0 + localUV.y * 15.0 + time * 2.0) * 0.5 + 0.5;
        shimmer = pow(shimmer, 3.0) * 0.15;
        gradient += vec3(shimmer);
        
        // Edge fade for depth
        float edgeFade = smoothstep(0.0, 20.0, -d);
        gradient *= 0.9 + edgeFade * 0.1;
        
        // Apply intensity
        gradient *= colorIntensity;
        
        result.rgb = gradient;
        result.a = opacity;
    }
    
    // Gradient border
    float borderDist = abs(d);
    float border = 1.0 - smoothstep(0.0, borderWidth + 1.0, borderDist);
    if (border > 0.0) {
        vec3 borderGrad = gradientColor(localUV + vec2(0.5), time * 1.3, color2, color1, waveScale * 0.7, contrast);
        vec3 bColor = mix(vec3(1.0), borderGrad, borderBlend);
        
        result.rgb = mix(result.rgb, bColor, border * 0.9);
        result.a = max(result.a, border * 0.9);
    }
    
    // Highlight glow
    if (isHighlighted && d > 0.0 && d < 25.0) {
        float glow = exp(-d * 0.12) * 0.5;
        vec3 glowColor = mix(color1, color2, sin(time) * 0.5 + 0.5);
        result.rgb += glowColor * glow;
        result.a = max(result.a, glow * 0.5);
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
        
        vec4 zoneColor = renderGradientZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        
        color.rgb = mix(color.rgb, zoneColor.rgb, zoneColor.a);
        color.a = max(color.a, zoneColor.a);
    }
    
    fragColor = vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
}
