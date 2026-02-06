// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>

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
    
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localPos = fragCoord - rectPos;

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    // Colors
    vec3 particleColor = colorWithFallback(customColors[0].rgb, fillColor.rgb);
    particleColor = colorWithFallback(particleColor, vec3(0.3, 0.8, 1.0));
    vec3 accentColor = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.4, 0.8));
    
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
        float angle = randOffset.x * TAU + time * (0.3 + randOffset.y * 0.4);
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
    float border = softBorder(d, borderWidth + 1.0);
    if (border > 0.0) {
        vec3 bColor = particleColor * borderGlow;
        result.rgb = mix(result.rgb, bColor, border * 0.8);
        result.a = max(result.a, border * 0.85);
    }
    
    // Highlight glow
    if (isHighlighted && d > 0.0 && d < 25.0) {
        float glow = expGlow(d, 10.0, 0.4);
        result.rgb += accentColor * glow;
        result.a = max(result.a, glow * 0.5);
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
        
        vec4 zoneColor = renderParticleZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);

    fragColor = clampFragColor(color);
}
