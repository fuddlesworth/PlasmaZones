// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out float vMouseInfluence;
layout(location = 2) out float vDistortAmount;
layout(location = 3) out vec2 vDisplacement;

layout(set = 0, binding = 0, std140) uniform ZoneUniforms {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int zoneCount;
    int highlightedCount;
    vec4 iMouse;        // xy = pixels, zw = normalized (0-1)
    // customParams[0]: fieldStrength, waveSpeed, rippleSize, glowIntensity
    // customParams[1]: particleCount, particleSize, trailLength, distortionAmount
    vec4 customParams[4];  // [0-3], access as customParams[0].x for slot 0, etc.
    vec4 customColors[8];  // [0-7], access as customColors[0] for color slot 0, etc.
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

// Noise for organic movement
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vTexCoord = texCoord;
    
    // Get parameters
    float fieldStrength = customParams[0].x > 0.01 ? customParams[0].x : 1.0;
    float waveSpeed = customParams[0].y > 0.01 ? customParams[0].y : 1.5;
    float distortionAmount = customParams[1].w > 0.01 ? customParams[1].w : 0.4;
    
    // Mouse position in normalized coordinates
    vec2 mouseNorm = iMouse.zw;
    
    // Current vertex position (normalized 0-1)
    vec2 vertexNorm = texCoord;
    
    // Calculate distance and direction to mouse
    vec2 toMouse = mouseNorm - vertexNorm;
    float distToMouse = length(toMouse);
    vec2 dirToMouse = distToMouse > 0.001 ? normalize(toMouse) : vec2(0.0);
    
    // Calculate edge factor (how close to edge of the quad)
    vec2 fromCenter = abs(vertexNorm - 0.5) * 2.0;
    float edgeFactor = max(fromCenter.x, fromCenter.y);
    edgeFactor = smoothstep(0.5, 1.0, edgeFactor);
    
    // =========================================================================
    // EFFECT 1: Magnetic Attraction
    // Edges pull toward the mouse position
    // =========================================================================
    float attractionStrength = fieldStrength * 0.03;
    float attractionFalloff = exp(-distToMouse * 3.0);
    vec2 attraction = dirToMouse * attractionStrength * attractionFalloff * edgeFactor;
    
    // =========================================================================
    // EFFECT 2: Ripple Wave from Mouse
    // Concentric waves emanate from mouse position
    // =========================================================================
    float rippleFreq = 15.0;
    float rippleSpeed = iTime * waveSpeed * 3.0;
    float ripple = sin(distToMouse * rippleFreq - rippleSpeed) * 0.5 + 0.5;
    ripple = pow(ripple, 2.0);
    float rippleFade = exp(-distToMouse * 4.0);
    vec2 rippleDisplacement = dirToMouse * ripple * 0.015 * rippleFade * fieldStrength * edgeFactor;
    
    // =========================================================================
    // EFFECT 3: Magnetic Field Distortion
    // Tangential movement creating field line appearance
    // =========================================================================
    vec2 tangent = vec2(-toMouse.y, toMouse.x);
    float fieldInfluence = exp(-distToMouse * 2.5) * fieldStrength * 0.5;
    float fieldWave = sin(iTime * waveSpeed * 2.0 + distToMouse * 10.0);
    vec2 fieldDistort = normalize(tangent + 0.001) * fieldWave * 0.008 * fieldInfluence * edgeFactor;
    
    // =========================================================================
    // EFFECT 4: Breathing/Pulsing toward mouse
    // The zone expands and contracts slightly toward mouse
    // =========================================================================
    float pulse = sin(iTime * waveSpeed * 1.5) * 0.5 + 0.5;
    float breatheAmount = 0.005 * pulse * fieldStrength;
    vec2 breathe = dirToMouse * breatheAmount * attractionFalloff * edgeFactor;
    
    // =========================================================================
    // EFFECT 5: Organic Noise Wobble
    // Adds organic feel to the deformation
    // =========================================================================
    float noiseScale = 3.0;
    float n1 = noise(vertexNorm * noiseScale + iTime * 0.5);
    float n2 = noise(vertexNorm * noiseScale + vec2(100.0) + iTime * 0.5);
    vec2 organicWobble = vec2(n1 - 0.5, n2 - 0.5) * 0.008 * edgeFactor * distortionAmount;
    // Increase wobble near mouse
    organicWobble *= (1.0 + attractionFalloff * 2.0);
    
    // =========================================================================
    // Combine all displacements
    // =========================================================================
    vec2 totalDisplacement = attraction + rippleDisplacement + fieldDistort + breathe + organicWobble;
    totalDisplacement *= distortionAmount;
    
    // Apply displacement to position (in clip space)
    vec2 displacedPos = position + totalDisplacement * 2.0; // *2 because clip space is -1 to 1
    
    // Pass data to fragment shader
    vMouseInfluence = attractionFalloff;
    vDistortAmount = length(totalDisplacement) * 50.0;
    vDisplacement = totalDisplacement;
    
    gl_Position = qt_Matrix * vec4(displacedPos, 0.0, 1.0);
}
