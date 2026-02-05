// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in float vMouseInfluence;
layout(location = 2) in float vDistortAmount;
layout(location = 3) in vec2 vDisplacement;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>

/**
 * MAGNETIC FIELD - Mouse-reactive shader
 *
 * Creates a magnetic/gravitational field effect that reacts to mouse position.
 * The field lines bend toward the cursor, creating an interactive visual.
 *
 * Parameters (customParams[0]):
 *   x = fieldStrength (0.0-2.0) - How strongly the field reacts to mouse
 *   y = waveSpeed (0.0-3.0) - Speed of field wave animations
 *   z = rippleSize (0.0-1.0) - Size of ripple effects from mouse
 *   w = glowIntensity (0.0-1.0) - Intensity of glow effects
 *
 * Parameters (customParams[1]):
 *   x = particleCount (10-100) - Number of field particles
 *   y = particleSize (0.5-3.0) - Size of particles
 *   z = trailLength (0.0-1.0) - Length of particle trails
 *   w = distortionAmount (0.0-1.0) - Amount of field distortion
 */

// Noise functions
#include <common.glsl>

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float f = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; i++) {
        f += amp * noise(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return f;
}

// Calculate field vector influenced by mouse
vec2 fieldVector(vec2 pos, vec2 mousePos, float strength, float time) {
    vec2 toMouse = mousePos - pos;
    float dist = length(toMouse);
    
    // Magnetic field lines curve around the mouse
    vec2 tangent = vec2(-toMouse.y, toMouse.x);
    float influence = strength / (dist * dist + 0.01);
    influence = clamp(influence, 0.0, 2.0);
    
    // Add some noise-based turbulence
    float turbulence = fbm(pos * 3.0 + time * 0.5) * 0.3;
    
    // Combine radial and tangential components
    vec2 field = normalize(tangent) * influence + normalize(toMouse) * influence * 0.3;
    field += vec2(cos(turbulence * 6.28), sin(turbulence * 6.28)) * 0.1;
    
    return field;
}

// Draw field lines
float fieldLines(vec2 pos, vec2 mousePos, float strength, float time, float speed) {
    float lines = 0.0;
    
    // Multiple field line layers
    for (int i = 0; i < 3; i++) {
        float phase = float(i) * 2.094; // 2π/3
        vec2 field = fieldVector(pos, mousePos, strength, time);
        
        // Create flowing field lines
        float angle = atan(field.y, field.x);
        float flow = sin(angle * 8.0 + length(pos - mousePos) * 10.0 - time * speed * 2.0 + phase);
        flow = pow(abs(flow), 3.0);
        
        // Distance-based fade
        float dist = length(pos - mousePos);
        float fade = exp(-dist * 2.0);
        
        lines += flow * fade * 0.4;
    }
    
    return lines;
}

// Ripple effect from mouse
float mouseRipple(vec2 pos, vec2 mousePos, float time, float size) {
    float dist = length(pos - mousePos);
    float ripple = sin(dist * 30.0 / size - time * 5.0);
    ripple = pow(max(ripple, 0.0), 2.0);
    float fade = exp(-dist * 3.0);
    return ripple * fade;
}

// Field particles that orbit mouse
float fieldParticles(vec2 pos, vec2 mousePos, float time, float count, float pSize) {
    float particles = 0.0;
    
    for (float i = 0.0; i < 50.0; i++) {
        if (i >= count) break;
        
        float angle = i * 2.399 + time * (0.5 + hash21(vec2(i, 0.0)) * 0.5);
        float radius = 0.05 + hash21(vec2(i, 1.0)) * 0.15;
        float speed = 0.8 + hash21(vec2(i, 2.0)) * 0.4;
        
        // Orbit around mouse
        vec2 orbit = mousePos + vec2(cos(angle * speed), sin(angle * speed)) * radius;
        
        // Add some wobble
        orbit += vec2(sin(time * 3.0 + i), cos(time * 2.0 + i * 1.5)) * 0.02;
        
        float dist = length(pos - orbit);
        float particle = smoothstep(pSize * 0.015, 0.0, dist);
        
        // Glow
        particle += exp(-dist * 100.0 / pSize) * 0.3;
        
        particles += particle;
    }
    
    return particles;
}

// Energy distortion around mouse
float energyDistortion(vec2 pos, vec2 mousePos, float time, float amount) {
    vec2 toMouse = pos - mousePos;
    float dist = length(toMouse);
    
    // Distortion waves
    float wave1 = sin(dist * 40.0 - time * 4.0) * 0.5 + 0.5;
    float wave2 = sin(dist * 25.0 - time * 3.0 + 1.0) * 0.5 + 0.5;
    
    float distortion = wave1 * wave2;
    distortion *= exp(-dist * 5.0) * amount;
    
    return distortion;
}

vec4 renderMagneticZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);
    
    // Parameters
    float fieldStrength = customParams[0].x > 0.01 ? customParams[0].x : 1.0;
    float waveSpeed = customParams[0].y > 0.01 ? customParams[0].y : 1.5;
    float rippleSize = customParams[0].z > 0.01 ? customParams[0].z : 0.5;
    float glowIntensity = customParams[0].w > 0.01 ? customParams[0].w : 0.6;
    
    float particleCount = customParams[1].x > 1.0 ? customParams[1].x : 30.0;
    float particleSize = customParams[1].y > 0.1 ? customParams[1].y : 1.5;
    float trailLength = customParams[1].z > 0.01 ? customParams[1].z : 0.5;
    float distortionAmount = customParams[1].w > 0.01 ? customParams[1].w : 0.4;
    
    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    
    // Mouse position relative to zone (normalized)
    vec2 mouseNorm = iMouse.zw;  // Already normalized 0-1
    vec2 mouseLocal = zoneLocalUV(iMouse.xy, rectPos, rectSize);  // Mouse relative to this zone
    
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    
    // Colors
    vec3 fieldColor = customColors[0].rgb;
    if (length(fieldColor) < 0.01) fieldColor = vec3(0.545, 0.361, 0.965);  // Purple #8b5cf6
    
    vec3 highlightColor = customColors[1].rgb;
    if (length(highlightColor) < 0.01) highlightColor = vec3(0.98, 0.8, 0.082);  // Yellow #facc15
    
    if (isHighlighted) {
        fieldColor = mix(fieldColor, highlightColor, 0.5);
        fieldStrength *= 1.5;
        glowIntensity *= 1.3;
    }
    
    float t = iTime * waveSpeed;
    
    vec4 result = vec4(0.0);
    
    if (d < 0.0) {
        // Dark background - tinted by vertex deformation
        vec3 bg = vec3(0.02, 0.03, 0.06);
        bg += fieldColor * vDistortAmount * 0.02; // Slight glow where deformed
        
        // Field lines emanating from mouse position
        float lines = fieldLines(localUV, mouseLocal, fieldStrength * 0.5, t, waveSpeed);
        
        // Mouse ripple effect
        float ripple = mouseRipple(localUV, mouseLocal, t, rippleSize);
        
        // Orbiting particles
        float particles = fieldParticles(localUV, mouseLocal, t, particleCount, particleSize);
        
        // Energy distortion - enhanced by vertex displacement
        float distortion = energyDistortion(localUV, mouseLocal, t, distortionAmount);
        distortion += vDistortAmount * 0.3; // Add vertex-based distortion visualization
        
        // Distance from mouse for radial effects
        float mouseDist = length(localUV - mouseLocal);
        
        // Central glow around mouse - enhanced by vertex influence
        float mouseGlow = exp(-mouseDist * 8.0) * glowIntensity;
        mouseGlow += vMouseInfluence * 0.2; // Vertices closer to mouse glow more
        
        // Pulsing aura
        float pulse = 0.5 + 0.5 * sin(t * 3.0);
        float aura = exp(-mouseDist * 4.0) * pulse * 0.3;
        
        // Deformation visualization - show where the mesh is being pulled
        vec3 deformColor = highlightColor * length(vDisplacement) * 15.0;
        
        // Combine effects
        vec3 fx = vec3(0.0);
        fx += fieldColor * lines * 0.8;
        fx += fieldColor * ripple * 0.6;
        fx += fieldColor * particles;
        fx += highlightColor * distortion * 0.5;
        fx += fieldColor * mouseGlow;
        fx += highlightColor * aura;
        fx += deformColor; // Add vertex deformation visualization
        
        // Subtle grid for depth - distorted by vertex displacement
        vec2 gridUV = localUV + vDisplacement * 2.0; // Grid follows deformation
        vec2 grid = abs(fract(gridUV * 20.0) - 0.5);
        float gridLine = smoothstep(0.48, 0.5, max(grid.x, grid.y)) * 0.05;
        fx += fieldColor * gridLine * (1.0 - mouseDist);
        
        // Strain lines - show direction of pull
        float strainAngle = atan(vDisplacement.y, vDisplacement.x);
        float strainLines = sin(strainAngle * 8.0 + length(vDisplacement) * 100.0) * 0.5 + 0.5;
        strainLines *= length(vDisplacement) * 20.0;
        fx += highlightColor * strainLines * 0.3;
        
        // Vignette - reduced in areas of high deformation
        float vig = 1.0 - length(localUV - 0.5) * (0.4 - vDistortAmount * 0.1);
        
        result.rgb = bg + fx * vig;
        result.a = 0.9 + vDistortAmount * 0.05; // Slightly more opaque where deformed
    }
    
    // Border with energy effect - enhanced by vertex displacement
    float effectiveBorderWidth = borderWidth + vDistortAmount * 2.0; // Border pulses with deformation
    float border = softBorder(d, effectiveBorderWidth);
    if (border > 0.0) {
        
        // Energy flowing along border - modulated by vertex influence
        float flow = sin(atan(p.y, p.x) * 8.0 + t * 4.0 + vMouseInfluence * 10.0) * 0.5 + 0.5;
        
        vec3 borderClr = mix(fieldColor, highlightColor, flow * 0.5 + vMouseInfluence * 0.3);
        borderClr *= 0.8 + 0.2 * flow + vDistortAmount * 0.5;
        
        result.rgb = mix(result.rgb, borderClr, border * 0.9);
        result.a = max(result.a, border * 0.95);
    }
    
    // Outer glow influenced by mouse proximity and vertex deformation
    float glowExtent = 25.0 + vDistortAmount * 15.0;
    if (d > 0.0 && d < glowExtent) {
        float mouseDist = length(localUV - mouseLocal);
        float mouseInfluence = exp(-mouseDist * 3.0);
        float glowSize = 15.0 + mouseInfluence * 10.0 + vDistortAmount * 5.0;
        float glow = exp(-d / glowSize) * (0.3 + mouseInfluence * 0.4 + vMouseInfluence * 0.2);
        result.rgb += fieldColor * glow * 0.4;
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
    
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;
        
        vec4 zoneColor = renderMagneticZone(fragCoord, rect, zoneFillColors[i], 
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);

    fragColor = clampFragColor(color);
}
