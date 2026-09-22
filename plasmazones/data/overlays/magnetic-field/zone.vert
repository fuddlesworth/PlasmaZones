// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out float vMouseInfluence;
layout(location = 2) out float vDistortAmount;
layout(location = 3) out vec2 vDisplacement;
layout(location = 4) out vec2 vFragCoord;

#include <common.glsl>

void main() {
    vTexCoord = texCoord;
    vFragCoord = fragCoordFromTexCoord(texCoord);

    // Get parameters
    float fieldStrength = customParams[0].x >= 0.0 ? customParams[0].x : 1.0;
    float waveSpeed = customParams[0].y >= 0.0 ? customParams[0].y : 1.5;
    // Slot 6, not 7. The p_<id> preamble is spliced into the FRAGMENT stage
    // only, so a vertex shader has to hand-decode the scalar pool, and the
    // slots follow declaration order in metadata.json:
    //   0 fieldStrength  1 waveSpeed  2 rippleSize  3 glowIntensity
    //   4 particleCount  5 particleSize  6 distortionAmount  7 audioReactivity
    // This read [1].w, which is audioReactivity. Every vertex displacement in
    // this file, and the vDistortAmount / vDisplacement varyings the fragment
    // stage builds its border width, glow extent and strain lines from, was
    // therefore scaled by the audio knob rather than the Distortion slider,
    // and defaulted to 1.0 where 0.4 was meant.
    float distortionAmount = customParams[1].z >= 0.0 ? customParams[1].z : 0.4;
    
    // Mouse position in normalized coordinates. iMouse.zw is fragCoord space,
    // and texCoord is the other orientation (texCoord.y == 1.0 - iMouse.w at
    // the same point), so normalising the vertex through vFragCoord instead of
    // texCoord is what keeps both stages agreeing. Reading texCoord here made
    // every mouse-driven vertex effect track the cursor mirrored about the
    // horizontal midline while this pack's own fragment stage did not.
    vec2 mouseNorm = iMouse.zw;
    
    // Current vertex position (normalized 0-1), same orientation as iMouse.zw
    vec2 vertexNorm = vFragCoord / max(iResolution, vec2(1.0));
    
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
    float n1 = noise2D(vertexNorm * noiseScale + iTime * 0.5);
    float n2 = noise2D(vertexNorm * noiseScale + vec2(100.0) + iTime * 0.5);
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
    
    gl_Position = vec4(displacedPos, 0.0, 1.0);
}
