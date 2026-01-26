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
    vec4 customColors[4];  // [0-3], access as customColors[0] for color slot 0, etc.
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

/*
 * FROSTED GLASS - Realistic Frosted/Etched Glass Effect
 * 
 * Simulates looking through frosted bathroom glass or ice.
 * Features distortion, crystalline texture, and refraction.
 * 
 * Parameters:
 *   customParams[0].x = frostDensity (2.0-8.0) - Frost crystal density
 *   customParams[0].y = frostOpacity (0.6-0.95) - How opaque the frost is
 *   customParams[0].z = distortionAmount (0.0-0.03) - Refraction distortion
 *   customParams[0].w = iceSharpness (1.0-4.0) - Crystal pattern sharpness
 *   customParams[1].x = edgeFrost (0.0-1.0) - Extra frost at edges
 *   customParams[1].y = shimmer (0.0-0.5) - Subtle light shimmer
 */

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// High quality value noise
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); // Quintic interpolation
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Fractal brownian motion for ice crystal texture
float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

// Voronoi-like pattern for ice crystals
float icePattern(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    
    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point = hash(i + neighbor) * 0.5 + 0.25 + neighbor;
            float dist = length(f - point);
            minDist = min(minDist, dist);
        }
    }
    return minDist;
}

vec4 renderFrostedGlassZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 1.5);
    
    float frostDensity = customParams[0].x > 0.5 ? customParams[0].x : 5.0;
    float frostOpacity = customParams[0].y > 0.1 ? customParams[0].y : 0.85;
    float distortionAmount = customParams[0].z > 0.0001 ? customParams[0].z : 0.015;
    float iceSharpness = customParams[0].w > 0.1 ? customParams[0].w : 2.5;
    float edgeFrost = customParams[1].x > 0.001 ? customParams[1].x : 0.4;
    float shimmer = customParams[1].y > 0.001 ? customParams[1].y : 0.2;
    
    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = (fragCoord - rectPos) / rectSize;
    
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    // Frost tint color
    vec3 frostTint = customColors[0].rgb;
    if (length(frostTint) < 0.01) frostTint = fillColor.rgb;
    if (length(frostTint) < 0.01) frostTint = vec3(0.94, 0.96, 0.97);  // #f0f4f8 cool white
    
    vec3 highlightTint = customColors[1].rgb;
    if (length(highlightTint) < 0.01) highlightTint = vec3(0.58, 0.64, 0.72);  // #94a3b8 slate
    
    if (isHighlighted) {
        frostTint = mix(frostTint, highlightTint, 0.3);
        frostOpacity = min(frostOpacity + 0.08, 0.98);
    }
    
    vec4 result = vec4(0.0);
    
    if (d < 0.0) {
        // Scale UV for frost patterns
        vec2 frostUV = localUV * frostDensity * 10.0;
        
        // Layer 1: Large ice crystal pattern (voronoi)
        float ice1 = icePattern(frostUV * 0.3);
        ice1 = pow(ice1, 1.0 / iceSharpness);
        
        // Layer 2: Medium frost texture
        float ice2 = icePattern(frostUV * 0.8 + 100.0);
        ice2 = pow(ice2, 1.0 / iceSharpness);
        
        // Layer 3: Fine grain frost
        float ice3 = fbm(frostUV * 2.0, 4);
        
        // Layer 4: Very fine noise
        float grain = fbm(frostUV * 5.0, 3);
        
        // Combine layers
        float frost = ice1 * 0.4 + ice2 * 0.3 + ice3 * 0.2 + grain * 0.1;
        
        // Add edge frosting (more frost near edges)
        float edgeDist = -d;
        float edgeFactor = exp(-edgeDist / 40.0) * edgeFrost;
        frost = mix(frost, 1.0, edgeFactor * 0.5);
        
        // Base white frost color
        vec3 frostColor = vec3(0.95, 0.97, 1.0);
        
        // Add blue-ish tint in the valleys
        frostColor = mix(frostColor, frostTint, (1.0 - frost) * 0.4);
        
        // Crystalline highlights
        float highlight = pow(frost, 3.0) * 0.4;
        frostColor += vec3(highlight);
        
        // Shimmer effect - light catching crystals
        if (shimmer > 0.01) {
            float shimmerPattern = sin(localUV.x * 100.0 + iTime * 0.5) * 
                                   sin(localUV.y * 80.0 + iTime * 0.3);
            shimmerPattern = pow(max(shimmerPattern, 0.0), 4.0);
            frostColor += vec3(shimmerPattern * shimmer);
        }
        
        // Top reflection (light from above)
        float topReflection = pow(1.0 - localUV.y, 2.0) * 0.15;
        frostColor += vec3(topReflection);
        
        // Distortion creates slight color variation
        vec2 distort = vec2(
            fbm(frostUV + vec2(50.0, 0.0), 3),
            fbm(frostUV + vec2(0.0, 50.0), 3)
        ) * distortionAmount;
        float distortColor = fbm((localUV + distort) * 20.0, 2);
        frostColor = mix(frostColor, frostColor * (0.9 + distortColor * 0.2), 0.3);
        
        // Depth variation
        float depth = fbm(frostUV * 0.5 + 200.0, 3);
        frostColor *= 0.85 + depth * 0.3;
        
        result.rgb = frostColor;
        result.a = frostOpacity;
    }
    
    // Frosted glass edge/border
    float borderDist = abs(d);
    if (borderDist < borderWidth + 2.0) {
        float border = 1.0 - smoothstep(0.0, borderWidth, borderDist);
        
        // Bright glass edge catching light
        vec3 edgeColor = vec3(1.0);
        edgeColor = mix(edgeColor, frostTint, 0.2);
        
        // Sharper at top
        float topBrightness = 1.0 + (1.0 - localUV.y) * 0.3;
        edgeColor *= topBrightness;
        
        result.rgb = mix(result.rgb, edgeColor, border * 0.7);
        result.a = max(result.a, border * 0.9);
    }
    
    // Inner edge frost line
    if (d < 0.0 && d > -8.0) {
        float innerEdge = 1.0 - (-d / 8.0);
        innerEdge = pow(innerEdge, 2.0);
        result.rgb = mix(result.rgb, vec3(1.0), innerEdge * 0.25);
    }
    
    // Highlight glow
    if (isHighlighted && d > 0.0 && d < 20.0) {
        float glow = exp(-d / 8.0) * 0.4;
        result.rgb += highlightTint * glow;
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
        
        vec4 zoneColor = renderFrostedGlassZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        
        color.rgb = mix(color.rgb, zoneColor.rgb, zoneColor.a);
        color.a = max(color.a, zoneColor.a);
    }
    
    fragColor = vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
}
