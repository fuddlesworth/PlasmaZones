// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in float vMouseInfluence;
layout(location = 2) in float vDistortAmount;
layout(location = 3) in vec2 vDisplacement;
layout(location = 4) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


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
// polarityFlip: -1.0 to 1.0, where negative values reverse tangent direction
vec2 fieldVector(vec2 pos, vec2 mousePos, float strength, float time, float polarityFlip) {
    vec2 toMouse = mousePos - pos;
    float dist = length(toMouse);

    // Guard against zero-length vector at exact mouse position
    if (dist < 0.001) return vec2(0.0);

    // Magnetic field lines curve around the mouse
    // Polarity flip reverses the tangent direction on strong bass hits
    // polarityFlip smoothly transitions through ~0 during reversal; use mix
    // instead of sign() to avoid a zero tangent (which breaks normalize)
    float polarity = polarityFlip >= 0.0 ? max(polarityFlip, 0.001) : min(polarityFlip, -0.001);
    vec2 tangent = vec2(-toMouse.y, toMouse.x) * polarity;
    float influence = strength / (dist * dist + 0.01);
    influence = clamp(influence, 0.0, 2.0);

    // Turbulence from noise (baseline, no longer audio-driven here)
    float turbulence = fbm(pos * 3.0 + time * 0.5) * 0.3;

    // Combine radial and tangential components
    // During polarity transition (~0), radial component wobbles creating chaotic field
    float radialSign = mix(1.0, -0.5, smoothstep(0.3, 0.0, abs(polarityFlip)));
    vec2 field = normalize(tangent) * influence + normalize(toMouse) * influence * 0.3 * radialSign;
    field += vec2(cos(turbulence * 6.28), sin(turbulence * 6.28)) * 0.1;

    return field;
}

// Draw field lines
float fieldLines(vec2 pos, vec2 mousePos, float strength, float time, float speed, float polarityFlip) {
    float lines = 0.0;

    // Field vector is independent of loop iteration — compute once
    vec2 field = fieldVector(pos, mousePos, strength, time, polarityFlip);
    float angle = atan(field.y, field.x);

    // Multiple field line layers
    for (int i = 0; i < 3; i++) {
        float phase = float(i) * 2.094; // 2π/3

        // Create flowing field lines
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

vec4 renderMagneticZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted,
                        float bass, float mids, float treble, float overall, bool hasAudio) {
    float borderRadius = max(params.x, 8.0);
    float borderWidth = max(params.y, 2.0);

    // Parameters
    float fieldStrength = customParams[0].x >= 0.0 ? customParams[0].x : 1.0;
    float waveSpeed = customParams[0].y >= 0.0 ? customParams[0].y : 1.5;
    float rippleSize = customParams[0].z >= 0.0 ? customParams[0].z : 0.5;
    float glowIntensity = customParams[0].w >= 0.0 ? customParams[0].w : 0.6;

    float particleCount = customParams[1].x >= 0.0 ? customParams[1].x : 30.0;
    float particleSize = customParams[1].y >= 0.0 ? customParams[1].y : 1.5;
    float trailLength = customParams[1].z >= 0.0 ? customParams[1].z : 0.5;
    float distortionAmount = customParams[1].w >= 0.0 ? customParams[1].w : 0.4;
    float audioReactivity  = customParams[2].x >= 0.0 ? customParams[2].x : 1.0;
    float nebulaIntensity  = customParams[2].y >= 0.0 ? customParams[2].y : 0.15;
    float shockwaveStr     = customParams[2].z >= 0.0 ? customParams[2].z : 1.0;
    float fillOpacity      = customParams[2].w >= 0.0 ? customParams[2].w : 0.85;

    // ── Identity-native audio: electromagnetic field behavior ─────

    // Bass = Field Polarity Reversal
    // Smooth polarity state: 1.0 = normal, crosses through 0 to -1.0 on bass hits
    // Uses a nonlinear curve so light bass doesn't flip, but strong bass snaps hard
    float bassThreshold = smoothstep(0.25, 0.7, bass);
    float polarityFlip = mix(1.0, -1.0, bassThreshold);

    // Bass shockwave ring expanding from mouse/center
    float shockwaveRadius = fract(iTime * 0.8 + bass * 0.5) * 1.2;
    float shockwaveStrength = bassThreshold * (1.0 - shockwaveRadius) * shockwaveStr;

    // Mids = EM interference band parameters
    float emBandIntensity = smoothstep(0.1, 0.5, mids);
    float emBandSpeed = 0.5 + mids * 3.0;

    // Treble = Corona discharge arc intensity
    float coronaIntensity = smoothstep(0.08, 0.5, treble);
    float coronaReach = 20.0 + treble * 60.0; // how far arcs extend from edge (pixels)

    // Overall = Field density/visibility (NOT a simple multiplier)
    // Affects rendering thresholds and particle visibility
    float fieldDensity = hasAudio ? 0.3 + overall * 1.4 * audioReactivity : 1.0;
    float visibleParticleCount = particleCount * fieldDensity;
    float fieldLineThreshold = mix(0.6, 0.15, fieldDensity); // lower = more lines visible

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);

    // Screen-space UV for continuous field across all zones
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));
    vec2 mouseGlobal = iMouse.xy / max(iResolution, vec2(1.0));

    // Mouse position relative to zone (for in-zone checks only)
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
        // ── Warped noise nebula background ──────────────────────────
        vec2 nebulaUV = globalUV * 3.0;
        // Warp the UV with a flowing offset
        float warpTime = iTime * 0.15;
        vec2 warp = vec2(
            fbm(nebulaUV + vec2(warpTime, 0.0)),
            fbm(nebulaUV + vec2(0.0, warpTime + 3.7))
        );
        float n1 = fbm(nebulaUV + warp * 1.5);
        float n2 = fbm(nebulaUV * 1.5 + warp + vec2(5.2, 1.3));
        float nebula = n1 * 0.6 + n2 * 0.4;

        // Color the nebula with dim fieldColor and highlightColor blend
        vec3 nebulaColor = mix(fieldColor * 0.4, highlightColor * 0.3, n2);
        float nebBright = (0.06 + nebula * 0.09) * (nebulaIntensity / 0.15);
        // Field density modulates nebula brightness
        nebBright += (fieldDensity - 0.5) * 0.04;

        vec3 bg = vec3(0.02, 0.03, 0.06) + nebulaColor * nebBright;
        bg += fieldColor * vDistortAmount * 0.02;

        // Field lines emanating from mouse position (polarity flip from bass)
        float lines = fieldLines(globalUV, mouseGlobal, fieldStrength * 0.5, t, waveSpeed, polarityFlip);
        // Field density controls line visibility threshold
        lines = smoothstep(fieldLineThreshold, fieldLineThreshold + 0.3, lines) * lines;

        // Mouse ripple effect
        float ripple = mouseRipple(globalUV, mouseGlobal, t, rippleSize);

        // Orbiting particles - visible count driven by overall energy
        float particles = fieldParticles(globalUV, mouseGlobal, t, visibleParticleCount, particleSize);

        // ── Energy connections between nearby particles ─────────────
        float connections = 0.0;
        for (float i = 0.0; i < 25.0; i++) {
            if (i >= visibleParticleCount) break;
            float ai = i * 2.399 + t * (0.5 + hash21(vec2(i, 0.0)) * 0.5);
            float ri = 0.05 + hash21(vec2(i, 1.0)) * 0.15;
            float si = 0.8 + hash21(vec2(i, 2.0)) * 0.4;
            vec2 pi = mouseGlobal + vec2(cos(ai * si), sin(ai * si)) * ri;
            pi += vec2(sin(t * 3.0 + i), cos(t * 2.0 + i * 1.5)) * 0.02;

            for (float j = i + 1.0; j < 25.0; j++) {
                if (j >= visibleParticleCount) break;
                float aj = j * 2.399 + t * (0.5 + hash21(vec2(j, 0.0)) * 0.5);
                float rj = 0.05 + hash21(vec2(j, 1.0)) * 0.15;
                float sj = 0.8 + hash21(vec2(j, 2.0)) * 0.4;
                vec2 pj = mouseGlobal + vec2(cos(aj * sj), sin(aj * sj)) * rj;
                pj += vec2(sin(t * 3.0 + j), cos(t * 2.0 + j * 1.5)) * 0.02;

                float pairDist = length(pi - pj);
                if (pairDist > 0.15) continue; // only connect nearby particles

                // Distance from fragment to the line segment pi->pj
                vec2 pa = globalUV - pi;
                vec2 ba = pj - pi;
                float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
                float lineDist = length(pa - ba * h);

                // Curved arc: offset perpendicular distance with a sine bulge
                float arcBulge = sin(h * PI) * pairDist * 0.3;
                lineDist = max(lineDist - arcBulge, 0.0);

                float arc = smoothstep(0.006, 0.0, lineDist);
                float proximity = 1.0 - pairDist / 0.15;
                connections += arc * proximity * 0.07;
            }
        }

        // Energy distortion - enhanced by vertex displacement
        float distortion = energyDistortion(globalUV, mouseGlobal, t, distortionAmount);
        distortion += vDistortAmount * 0.3;

        // Distance from mouse for radial effects
        float mouseDist = length(globalUV - mouseGlobal);

        // Central glow around mouse - enhanced by vertex influence
        float mouseGlow = exp(-mouseDist * 8.0) * glowIntensity;
        mouseGlow += vMouseInfluence * 0.2;

        // Pulsing aura
        float pulse = 0.5 + 0.5 * sin(t * 3.0);
        float aura = exp(-mouseDist * 4.0) * pulse * 0.3;

        // Deformation visualization
        vec3 deformColor = highlightColor * length(vDisplacement) * 15.0;

        // Combine effects - modulated by fieldDensity for overall intensity
        vec3 fx = vec3(0.0);
        fx += fieldColor * lines * 0.8 * fieldDensity;
        fx += fieldColor * ripple * 0.6;
        fx += fieldColor * particles * fieldDensity;
        fx += mix(fieldColor, highlightColor, 0.3) * connections * fieldDensity;
        fx += highlightColor * distortion * 0.5;
        fx += fieldColor * mouseGlow;
        fx += highlightColor * aura;
        fx += deformColor;

        // Subtle grid for depth - distorted by vertex displacement
        vec2 gridUV = globalUV + vDisplacement * 2.0;
        vec2 grid = abs(fract(gridUV * 20.0) - 0.5);
        float gridLine = smoothstep(0.48, 0.5, max(grid.x, grid.y)) * 0.05;
        fx += fieldColor * gridLine * (1.0 - mouseDist) * fieldDensity;

        // Strain lines - show direction of pull
        float strainAngle = atan(vDisplacement.y, vDisplacement.x);
        float strainLines = sin(strainAngle * 8.0 + length(vDisplacement) * 100.0) * 0.5 + 0.5;
        strainLines *= length(vDisplacement) * 20.0;
        fx += highlightColor * strainLines * 0.3;

        // ── BASS: Polarity reversal shockwave ring ──────────────────
        if (hasAudio && shockwaveStrength > 0.01) {
            float shockDist = length(globalUV - mouseGlobal);
            // Ring at the expanding shockwave radius
            float ringWidth = 0.015 + bassThreshold * 0.02;
            float ring = exp(-pow(shockDist - shockwaveRadius * 0.4, 2.0) / (ringWidth * ringWidth));
            // Ring color: bright white-blue flash during polarity reversal
            vec3 shockColor = mix(fieldColor, vec3(0.7, 0.85, 1.0), 0.7);
            fx += shockColor * ring * shockwaveStrength * 1.5;
            // Secondary fainter ring trailing behind
            float ring2 = exp(-pow(shockDist - shockwaveRadius * 0.3, 2.0) / (ringWidth * ringWidth * 4.0));
            fx += shockColor * ring2 * shockwaveStrength * 0.4;
        }

        // ── MIDS: EM Interference Bands ─────────────────────────────
        if (hasAudio && emBandIntensity > 0.01) {
            // Diagonal sweep bands moving through the zone
            float bandAngle = 0.3; // slight diagonal
            float bandCoord = globalUV.y * cos(bandAngle) + globalUV.x * sin(bandAngle);
            // Multiple band frequencies for EM interference look
            float sweep = iTime * emBandSpeed * 0.3;
            float band1 = sin(bandCoord * 25.0 - sweep) * 0.5 + 0.5;
            float band2 = sin(bandCoord * 40.0 - sweep * 1.7 + 1.5) * 0.5 + 0.5;
            float band3 = sin(bandCoord * 15.0 - sweep * 0.6 + 3.0) * 0.5 + 0.5;
            // Combine for interference pattern
            float emBand = band1 * band2 * 0.7 + band3 * 0.3;
            emBand = pow(emBand, 2.0); // sharpen the bands
            // Chromatic aberration: offset R and B channels for color fringing
            float bandR = sin((bandCoord - 0.003) * 25.0 - sweep) * 0.5 + 0.5;
            float bandB = sin((bandCoord + 0.003) * 25.0 - sweep) * 0.5 + 0.5;
            vec3 fringedBand = vec3(
                bandR * band2 * 0.7 + band3 * 0.3,
                emBand,
                bandB * band2 * 0.7 + band3 * 0.3
            );
            fringedBand = pow(fringedBand, vec3(2.0));
            fx += fringedBand * emBandIntensity * 0.35;
        }

        // ── TREBLE: Corona Discharge Arcs at zone edges ─────────────
        float edgeDist = -d; // positive inside, largest at center
        if (hasAudio && coronaIntensity > 0.01) {
            float edgeProx = smoothstep(coronaReach, 0.0, edgeDist);
            if (edgeProx > 0.01) {
                // Angular coordinate in screen space for continuous arc pattern
                vec2 fromCenter = globalUV - 0.5;
                float angle = atan(fromCenter.y, fromCenter.x);

                // Multi-scale angular noise creates branching arc structure
                float arcNoise1 = noise(vec2(angle * 3.0, iTime * 4.0)) * 0.5;
                float arcNoise2 = noise(vec2(angle * 8.0, iTime * 6.0 + 10.0)) * 0.3;
                float arcNoise3 = noise(vec2(angle * 20.0, iTime * 8.0 + 25.0)) * 0.2;
                float arcPattern = arcNoise1 + arcNoise2 + arcNoise3;

                // Arcs are strongest near the edge, branch inward
                // The noise threshold determines which angles get arcs
                float arcThreshold = 0.55 - coronaIntensity * 0.25;
                float arc = smoothstep(arcThreshold, arcThreshold + 0.15, arcPattern);

                // Arc fades with distance from edge (brighter at edge, dims inward)
                float arcFade = pow(edgeProx, 1.5);
                arc *= arcFade;

                // Flickering intensity (electrical discharge is unstable)
                float flicker = 0.7 + 0.3 * noise(vec2(angle * 5.0 + iTime * 15.0, iTime * 10.0));
                arc *= flicker;

                // Corona color: bright electric blue-white core with purple fringe
                vec3 arcCore = vec3(0.75, 0.85, 1.0);
                vec3 arcFringe = fieldColor * 1.5;
                vec3 coronaColor = mix(arcFringe, arcCore, arc * 0.8);

                fx += coronaColor * arc * coronaIntensity * 0.8;
            }
        }

        // Vignette - reduced in areas of high deformation
        float vig = 1.0 - length(globalUV - 0.5) * (0.4 - vDistortAmount * 0.1);

        result.rgb = bg + fx * vig;
        result.a = fillOpacity + vDistortAmount * 0.05;
    }

    // Border with energy effect - enhanced by vertex displacement
    float effectiveBorderWidth = borderWidth + vDistortAmount * 2.0;
    float border = softBorder(d, effectiveBorderWidth);
    if (border > 0.0) {

        // Energy flowing along border - modulated by vertex influence
        float flow = sin(atan(p.y, p.x) * 8.0 + t * 4.0 + vMouseInfluence * 10.0) * 0.5 + 0.5;

        vec3 borderClr = mix(fieldColor, highlightColor, flow * 0.5 + vMouseInfluence * 0.3);
        borderClr *= 0.8 + 0.2 * flow + vDistortAmount * 0.5;
        // Corona discharge bleeds into border during treble
        vec3 coronaBorderGlow = vec3(0.6, 0.75, 1.0) * coronaIntensity * 0.4;
        borderClr += coronaBorderGlow;

        result.rgb = mix(result.rgb, borderClr, border * 0.9);
        result.a = max(result.a, border * 0.95);
    }

    // Outer glow influenced by mouse proximity and vertex deformation
    float glowExtent = 25.0 + vDistortAmount * 15.0;
    if (d > 0.0 && d < glowExtent) {
        float mouseDist = length(globalUV - mouseGlobal);
        float mouseInfluence = exp(-mouseDist * 3.0);
        float glowSize = 15.0 + mouseInfluence * 10.0 + vDistortAmount * 5.0;
        float glow = exp(-d / glowSize) * (0.3 + mouseInfluence * 0.4 + vMouseInfluence * 0.2);
        result.rgb += fieldColor * glow * 0.4;
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
    
    // Audio analysis (computed once for all zones)
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBass();
    float mids    = getMids();
    float treble  = getTreble();
    float overall = getOverall();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderMagneticZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);
        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);

    fragColor = clampFragColor(color);
}
