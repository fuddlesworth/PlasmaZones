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
    // customParams[0]: surfaceOpacity, causticIntensity, depthColor, highlightStrength
    // customParams[1]: warpAmount, warpFrequency, warpSpeed, borderWobble
    // customParams[2]: breatheAmount, breatheSpeed, unused, unused
    vec4 customParams[4];  // [0-3], access as customParams[0].x for slot 0, etc.
    // customColors[0]: waterColor, customColors[1]: causticColor
    vec4 customColors[8];  // [0-7], access as customColors[0] for color slot 0, etc.
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

const float PI = 3.14159265359;

// Noise functions for organic movement
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
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

float fbm(vec2 p, int octaves) {
    float f = 0.0;
    float amp = 0.5;
    for (int i = 0; i < octaves; i++) {
        f += amp * noise(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return f;
}

// Distorted SDF for rounded box - this creates the liquid deformation
float sdRoundedBoxDistorted(vec2 p, vec2 b, float r, float time, float warpAmount, float warpFreq) {
    // Apply distortion to the distance calculation
    // This warps the boundary of the zone
    
    float angle = atan(p.y, p.x);
    float dist = length(p);
    
    // Edge ripples - waves traveling around the perimeter
    float ripple1 = sin(angle * warpFreq + time * 2.0) * warpAmount * 0.6;
    float ripple2 = sin(angle * warpFreq * 1.618 - time * 1.5) * warpAmount * 0.3;
    float ripple3 = sin(angle * warpFreq * 0.618 + time * 2.5) * warpAmount * 0.2;
    
    // Noise-based organic wobble
    float wobble = (noise(vec2(angle * 2.0 + time * 0.5, time * 0.3)) - 0.5) * warpAmount * 0.8;
    
    // Breathing effect
    float breathe = sin(time * 1.2) * warpAmount * 0.3;
    
    // Total warp applied to the box size
    float totalWarp = ripple1 + ripple2 + ripple3 + wobble + breathe;
    
    // Apply warp to box dimensions - this actually deforms the shape
    vec2 warpedB = b + vec2(totalWarp);
    
    // Standard rounded box SDF with warped dimensions
    vec2 q = abs(p) - warpedB + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Additional per-edge distortion
vec2 edgeDistortion(vec2 p, vec2 b, float time, float amount) {
    vec2 distort = vec2(0.0);
    
    // Check proximity to each edge and apply directional waves
    float edgeThreshold = 0.9;
    
    // Left/Right edges - horizontal waves
    float nearVerticalEdge = smoothstep(b.x * edgeThreshold, b.x, abs(p.x));
    distort.x += sin(p.y * 15.0 + time * 3.0) * amount * nearVerticalEdge * 8.0;
    
    // Top/Bottom edges - vertical waves  
    float nearHorizontalEdge = smoothstep(b.y * edgeThreshold, b.y, abs(p.y));
    distort.y += sin(p.x * 15.0 + time * 2.5) * amount * nearHorizontalEdge * 8.0;
    
    return distort;
}

// Caustic light pattern
float caustics(vec2 uv, float time) {
    float c = 0.0;
    
    // Multiple layers of caustic patterns
    for (int i = 0; i < 3; i++) {
        float scale = 8.0 + float(i) * 4.0;
        float speed = 0.5 + float(i) * 0.3;
        vec2 p = uv * scale + vec2(time * speed, time * speed * 0.7);
        
        float n1 = noise(p);
        float n2 = noise(p + vec2(5.2, 1.3));
        float n3 = noise(p * 1.5 + vec2(time * 0.2, 0.0));
        
        float caustic = abs(sin(n1 * 6.28) * sin(n2 * 6.28));
        caustic = pow(caustic, 2.0);
        caustic *= (0.5 + 0.5 * n3);
        
        c += caustic * (0.5 / float(i + 1));
    }
    
    return c;
}

vec4 renderLiquidZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);
    
    // Parameters
    float surfaceOpacity = customParams[0].x > 0.01 ? customParams[0].x : 0.82;
    float causticIntensity = customParams[0].y > 0.01 ? customParams[0].y : 0.45;
    float depthDarken = customParams[0].z > 0.01 ? customParams[0].z : 0.2;
    float highlightStrength = customParams[0].w > 0.01 ? customParams[0].w : 0.4;
    
    float warpAmount = customParams[1].x > 0.001 ? customParams[1].x : 0.015;
    float warpFrequency = customParams[1].y > 0.5 ? customParams[1].y : 6.0;
    float warpSpeed = customParams[1].z > 0.1 ? customParams[1].z : 2.0;
    float borderWobble = customParams[1].w > 0.001 ? customParams[1].w : 0.008;
    
    float breatheAmount = customParams[2].x > 0.0001 ? customParams[2].x : 0.008;
    
    // Convert rect to pixel coordinates
    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 halfSize = rectSize * 0.5;
    
    // Position relative to zone center
    vec2 p = fragCoord - center;
    
    // Scale warp amount based on zone size for consistent visual effect
    float sizeScale = min(rectSize.x, rectSize.y) / 200.0;
    float scaledWarp = warpAmount * sizeScale * 50.0;
    
    // Apply edge distortion to sample position
    vec2 edgeDistort = edgeDistortion(p, halfSize, iTime * warpSpeed, borderWobble);
    vec2 distortedP = p + edgeDistort;
    
    // Calculate distorted SDF
    float d = sdRoundedBoxDistorted(distortedP, halfSize, borderRadius, iTime * warpSpeed, scaledWarp, warpFrequency);
    
    // Local UV for effects
    vec2 localUV = (fragCoord - rectPos) / rectSize;
    
    // Colors
    vec3 waterColor = customColors[0].rgb;
    if (length(waterColor) < 0.01) waterColor = fillColor.rgb;
    if (length(waterColor) < 0.01) waterColor = vec3(0.05, 0.31, 0.43);  // #0d4f6e deep teal
    
    vec3 causticColor = customColors[1].rgb;
    if (length(causticColor) < 0.01) causticColor = vec3(0.37, 0.92, 0.83);  // #5eead4 teal
    
    if (isHighlighted) {
        waterColor = mix(waterColor, causticColor, 0.3);
        causticIntensity *= 1.4;
        highlightStrength *= 1.3;
    }
    
    vec4 result = vec4(0.0);
    
    // Inside the zone
    if (d < 0.0) {
        // Base water color with depth variation
        float depthFactor = 1.0 - length(localUV - 0.5) * depthDarken;
        vec3 color = waterColor * depthFactor;
        
        // Caustic lighting
        float caust = caustics(localUV, iTime) * causticIntensity;
        color += causticColor * caust;
        
        // Surface ripples - distorted by the warp
        vec2 rippleUV = localUV + edgeDistort * 0.001;
        float ripple = noise(rippleUV * 20.0 + iTime * 0.5);
        color += causticColor * ripple * 0.08;
        
        // Fresnel-like edge brightening
        float edgeDist = -d / 30.0;
        float fresnel = pow(1.0 - clamp(edgeDist, 0.0, 1.0), 2.0);
        color += causticColor * fresnel * 0.15;
        
        // Specular highlights
        float spec = pow(noise(localUV * 30.0 + vec2(iTime * 0.3, iTime * 0.2)), 4.0);
        color += vec3(1.0) * spec * highlightStrength * 0.3;
        
        // Top reflection
        float topRef = pow(1.0 - localUV.y, 3.0) * 0.1;
        color += causticColor * topRef;
        
        result.rgb = color;
        result.a = surfaceOpacity;
    }
    
    // Wobbly border
    float borderDist = abs(d);
    float wobbleOffset = sin(atan(distortedP.y, distortedP.x) * 8.0 + iTime * 3.0) * borderWobble * sizeScale * 20.0;
    float effectiveBorderWidth = borderWidth + wobbleOffset;
    
    if (borderDist < effectiveBorderWidth + 2.0) {
        float border = 1.0 - smoothstep(0.0, effectiveBorderWidth, borderDist);
        
        // Animated border color
        float flow = sin(atan(distortedP.y, distortedP.x) * 6.0 - iTime * 2.0) * 0.5 + 0.5;
        vec3 borderClr = mix(waterColor, causticColor, flow * 0.6);
        borderClr *= 1.0 + flow * 0.3;
        
        result.rgb = mix(result.rgb, borderClr, border * 0.9);
        result.a = max(result.a, border * 0.95);
    }
    
    // Outer glow / meniscus
    if (d > 0.0 && d < 15.0) {
        float glow = exp(-d / 6.0) * 0.4;
        result.rgb += causticColor * glow * 0.3;
        result.a = max(result.a, glow * 0.3);
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
        
        vec4 zoneColor = renderLiquidZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        
        // Alpha compositing
        color.rgb = mix(color.rgb, zoneColor.rgb, zoneColor.a);
        color.a = max(color.a, zoneColor.a);
    }
    
    fragColor = vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
}
