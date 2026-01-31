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
 * PARTICLE FIELD - Floating Particles Effect
 * 
 * Animated particles floating within zones with glow effects.
 * Particle count scales with zone area for consistent visual density.
 * 
 * Parameters:
 *   customParams[0].x = particleDensity (5.0-150.0) - Base particle count (scales with zone size)
 *   customParams[0].y = particleSpeed (0.2-2.0) - Movement speed
 *   customParams[0].z = particleSize (3.0-15.0) - Particle radius
 *   customParams[0].w = glowAmount (0.5-2.0) - Particle glow intensity
 *   customParams[1].x = fillOpacity (0.1-0.5) - Background fill
 *   customParams[1].y = borderGlow (0.3-1.0) - Border brightness
 */

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Quality random functions
float hash(float n) { return fract(sin(n) * 43758.5453123); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec4 renderParticleZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 6.0);
    float borderWidth = max(params.y, 2.0);
    
    float particleDensity = customParams[0].x > 1.0 ? customParams[0].x : 25.0;
    float particleSpeed = customParams[0].y > 0.01 ? customParams[0].y : 0.8;
    float particleSize = customParams[0].z > 0.5 ? customParams[0].z : 6.0;
    float glowAmount = customParams[0].w > 0.1 ? customParams[0].w : 1.2;
    float fillOpacity = customParams[1].x > 0.01 ? customParams[1].x : 0.25;
    float borderGlow = customParams[1].y > 0.1 ? customParams[1].y : 0.7;
    float fillDarkness = customParams[1].z > 0.001 ? customParams[1].z : 0.1;
    float particleBrightness = customParams[1].w > 0.01 ? customParams[1].w : 0.25;
    
    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localPos = fragCoord - rectPos;
    vec2 localUV = localPos / rectSize;
    
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    // Colors
    vec3 particleColor = customColors[0].rgb;
    if (length(particleColor) < 0.01) particleColor = fillColor.rgb;
    if (length(particleColor) < 0.01) particleColor = vec3(0.3, 0.8, 1.0);
    
    vec3 accentColor = customColors[1].rgb;
    if (length(accentColor) < 0.01) accentColor = vec3(1.0, 0.4, 0.8);
    
    if (isHighlighted) {
        particleColor = mix(particleColor, accentColor, 0.5);
        particleDensity *= 1.3;
        glowAmount *= 1.3;
    }
    
    vec4 result = vec4(0.0);
    
    // Dark fill
    if (d < 0.0) {
        result.rgb = particleColor * fillDarkness;
        result.a = fillOpacity;
    }
    
    // Render particles
    float time = iTime * particleSpeed;
    float particleField = 0.0;
    
    // Scale particle count by zone area for consistent density feel.
    // Baseline: 40000 = 200x200 px zone; sqrt so density doesn't explode on large zones.
    // Caps: numParticles <= 200 (performance), particleField <= 3.5 (avoid brightness blowout).
    float zoneArea = rectSize.x * rectSize.y;
    float areaScale = sqrt(zoneArea / 40000.0);
    int numParticles = int(particleDensity * max(areaScale, 0.5));
    numParticles = min(numParticles, 200);
    
    for (int i = 0; i < numParticles && i < 200; i++) {
        float fi = float(i);
        
        // Unique particle properties
        vec2 seed = vec2(fi * 0.123, fi * 0.456);
        vec2 randOffset = hash22(seed);
        
        // Particle movement - floating motion
        float angle = randOffset.x * 6.28318 + time * (0.3 + randOffset.y * 0.4);
        float radius = 0.1 + randOffset.y * 0.3;
        vec2 motion = vec2(cos(angle), sin(angle * 0.7 + fi)) * radius;
        
        // Base position
        vec2 basePos = hash22(seed + 100.0);
        vec2 particlePos = basePos + motion * 0.15;
        particlePos = fract(particlePos); // Wrap around
        
        // Scale to zone size
        vec2 pPos = particlePos * rectSize;
        
        // Distance to particle
        float dist = length(localPos - pPos);
        
        // Particle with sharper core and soft glow
        float pSize = particleSize * (0.7 + randOffset.x * 0.6);
        float particle = exp(-dist * dist / (pSize * pSize * 1.5)); // Sharper core
        
        // Outer glow (reduced for more distinct particles)
        float glow = exp(-dist / (pSize * 2.5)) * 0.25;
        
        particleField += (particle * 1.2 + glow * glowAmount) * (0.6 + randOffset.y * 0.4);
    }
    
    // Apply particles (cap 3.5 avoids brightness blowout when many particles overlap)
    if (d < 0.0) {
        particleField = min(particleField, 3.5);
        vec3 pColor = particleColor * particleField;
        // White core for bright particles
        pColor = mix(pColor, vec3(1.0), particleField * particleBrightness);
        result.rgb += pColor;
        result.a = max(result.a, min(particleField * 0.6 + 0.1, 0.95));
    }
    
    // Border
    float borderDist = abs(d);
    float border = 1.0 - smoothstep(0.0, borderWidth + 1.0, borderDist);
    if (border > 0.0) {
        vec3 bColor = particleColor * borderGlow;
        result.rgb = mix(result.rgb, bColor, border * 0.8);
        result.a = max(result.a, border * 0.85);
    }
    
    // Highlight glow
    if (isHighlighted && d > 0.0 && d < 25.0) {
        float glow = exp(-d * 0.1) * 0.4;
        result.rgb += accentColor * glow;
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
        
        vec4 zoneColor = renderParticleZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        
        // Alpha compositing (over operator for overlapping zones)
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
