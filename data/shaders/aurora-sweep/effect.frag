// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Aurora Sweep - Clean gradient with animated light sweep
// Elegant, minimal overlay effect with smooth color transitions

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform ZoneUniforms {
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

// === PARAMETER ACCESS ===
// Slot 0: Animation speed
// Slot 1: Gradient angle (0-360 degrees)
// Slot 2: Fill opacity
// Slot 3: Sweep intensity
// Slot 4: Sweep width
// Slot 5: Border width
// Slot 6: Sweep interval (seconds between sweeps)
// Slot 7: Color blend mode (0=linear, 1=smooth)

float getAnimSpeed()     { return customParams[0].x > 0.01 ? customParams[0].x : 1.0; }
float getGradientAngle() { return customParams[0].y > 0.01 ? customParams[0].y : 135.0; }
float getFillOpacity()   { return customParams[0].z > 0.01 ? customParams[0].z : 0.35; }
float getSweepIntensity(){ return customParams[0].w > 0.01 ? customParams[0].w : 0.25; }
float getSweepWidth()    { return customParams[1].x > 0.01 ? customParams[1].x : 0.15; }
float getBorderWidth()   { return customParams[1].y > 0.1 ? customParams[1].y : 2.0; }
float getSweepInterval() { return customParams[1].z > 0.1 ? customParams[1].z : 3.0; }
// 0 = linear gradient blend, non-zero = smooth step; no default so 0 is valid (linear)
float getBlendMode()     { return customParams[1].w; }
float getShimmerIntensity() { return customParams[2].x > 0.001 ? customParams[2].x : 0.08; }

// Color 0: Gradient start color (default: deep blue-purple)
vec3 getColor1() {
    vec3 c = customColors[0].rgb;
    return length(c) > 0.01 ? c : vec3(0.20, 0.25, 0.55);  // #333D8C
}

// Color 1: Gradient end color (default: cyan-teal)
vec3 getColor2() {
    vec3 c = customColors[1].rgb;
    return length(c) > 0.01 ? c : vec3(0.25, 0.65, 0.85);  // #40A6D9
}

// Color 2: Sweep highlight color (default: white with slight cyan)
vec3 getSweepColor() {
    vec3 c = customColors[2].rgb;
    return length(c) > 0.01 ? c : vec3(0.85, 0.95, 1.0);
}

// Color 3: Border color (default: uses gradient midpoint)
vec3 getBorderColor() {
    vec3 c = customColors[3].rgb;
    return length(c) > 0.01 ? c : mix(getColor1(), getColor2(), 0.6);
}

#include <common.glsl>

// Smooth gradient based on angle
float gradientValue(vec2 uv, float angleDeg) {
    float angle = radians(angleDeg);
    vec2 dir = vec2(cos(angle), sin(angle));
    
    // Project UV onto gradient direction
    float t = dot(uv - 0.5, dir) + 0.5;
    return clamp(t, 0.0, 1.0);
}

// Animated sweep effect - a bright band that travels across the zone
float sweepEffect(vec2 uv, float time, float interval, float width) {
    // Calculate sweep position with interval pause
    float cycleTime = mod(time, interval);
    float sweepDuration = interval * 0.4;  // Sweep takes 40% of cycle
    
    // Only sweep during active portion
    if (cycleTime > sweepDuration) {
        return 0.0;
    }
    
    float sweepProgress = cycleTime / sweepDuration;
    
    // Sweep position moves from -width to 1+width
    float sweepPos = mix(-width, 1.0 + width, sweepProgress);
    
    // Diagonal sweep (45 degree angle)
    float diagPos = (uv.x + uv.y) * 0.5;
    
    // Create soft-edged sweep band
    float dist = abs(diagPos - sweepPos);
    float sweep = 1.0 - smoothstep(0.0, width, dist);
    
    // Taper at edges for smoother appearance
    sweep *= smoothstep(0.0, 0.3, sweepProgress);
    sweep *= smoothstep(1.0, 0.7, sweepProgress);
    
    return sweep * sweep;  // Quadratic falloff for softer look
}

// Subtle shimmer/sparkle overlay
float shimmer(vec2 uv, float time) {
    float intensity = getShimmerIntensity();
    float s1 = sin(uv.x * 25.0 + time * 2.0) * 0.5 + 0.5;
    float s2 = sin(uv.y * 30.0 - time * 1.5) * 0.5 + 0.5;
    float s3 = sin((uv.x + uv.y) * 20.0 + time * 3.0) * 0.5 + 0.5;

    float combined = s1 * s2 * s3;
    return pow(combined, 4.0) * intensity;
}

vec4 renderAuroraSweepZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 8.0);
    float zoneBorderWidth = max(params.y, 2.0);
    
    float animSpeed = getAnimSpeed();
    float gradAngle = getGradientAngle();
    float fillOpacity = getFillOpacity();
    float sweepIntensity = getSweepIntensity();
    float sweepWidth = getSweepWidth();
    float borderWidth = getBorderWidth();
    float sweepInterval = getSweepInterval();
    float blendMode = getBlendMode();
    
    vec3 color1 = getColor1();
    vec3 color2 = getColor2();
    vec3 sweepColor = getSweepColor();
    vec3 borderClr = getBorderColor();
    
    // Convert normalized coords to pixels
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    
    // Local UV within the zone (0-1)
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    localUV = clamp(localUV, 0.0, 1.0);
    
    // Signed distance to zone boundary
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    float time = iTime * animSpeed;
    
    vec4 result = vec4(0.0);
    
    // Inside the zone
    if (d < 0.0) {
        // Calculate gradient position
        float gradT = gradientValue(localUV, gradAngle);
        
        // Apply blend mode
        if (blendMode > 0.5) {
            // Smooth step blend
            gradT = smoothstep(0.0, 1.0, gradT);
        }
        
        // Base gradient color
        vec3 baseColor = mix(color1, color2, gradT);
        
        // Add subtle time-based color shift
        float colorShift = sin(time * 0.3) * 0.03;
        baseColor = mix(baseColor, color2, colorShift);
        
        // Add shimmer
        float shim = shimmer(localUV, time);
        baseColor += vec3(shim);
        
        // Calculate and add sweep effect
        float sweep = sweepEffect(localUV, time, sweepInterval, sweepWidth);
        baseColor = mix(baseColor, sweepColor, sweep * sweepIntensity);
        
        // Edge darkening for depth
        float edgeDist = -d / min(rectSize.x, rectSize.y) * 2.0;
        float edgeDarken = smoothstep(0.0, 0.15, edgeDist);
        baseColor *= 0.95 + edgeDarken * 0.05;
        
        result.rgb = baseColor;
        result.a = fillOpacity;
        
        // Brighten highlighted zones
        if (isHighlighted) {
            result.rgb *= 1.2;
            result.a = min(result.a + 0.15, 0.85);
        }
    }
    
    // Border rendering
    float border = softBorder(d, borderWidth);
    if (border > 0.0) {
        
        // Use custom border color or derive from gradient
        vec3 bColor = borderClr;
        if (length(borderColor.rgb) > 0.01) {
            bColor = borderColor.rgb;
        }
        
        // Add subtle glow to border
        float borderGlow = 1.0 + sin(time * 2.0) * 0.1;
        bColor *= borderGlow;
        
        result.rgb = mix(result.rgb, bColor, border * 0.9);
        result.a = max(result.a, border * 0.9);
        
        // Brighter border for highlighted zones
        if (isHighlighted) {
            result.rgb = mix(result.rgb, sweepColor, border * 0.3);
        }
    }
    
    // Outer glow for highlighted zones
    if (isHighlighted && d > 0.0 && d < 20.0) {
        float glow = expGlow(d, 8.0, 0.35);
        vec3 glowColor = mix(color1, color2, 0.5);
        result.rgb += glowColor * glow;
        result.a = max(result.a, glow * 0.5);
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
    
    // Render each zone
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        
        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderAuroraSweepZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);

    fragColor = clampFragColor(color);
}
