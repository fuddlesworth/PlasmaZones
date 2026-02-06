// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>

/*
 * TOXIC CIRCUIT - Cyberpunk Venom Overlay Effect
 * Inspired by Neon Venom / Pokemon: Toxic Circuit aesthetic
 *
 * Structure: diagonalCircuitGrid + circuitPattern (traces/nodes), toxicDrip,
 * glitchOffset, atmosphericSmog; then renderToxicCircuitZone (base + circuits +
 * drip + smog + shimmer + mouse interaction). Parameters:
 *   customParams[0].x = circuitDensity (4.0-20.0) - Circuit trace density
 *   customParams[0].y = pulseSpeed (0.5-3.0) - Energy pulse animation speed
 *   customParams[0].z = dripIntensity (0.0-1.0) - Toxic drip effect strength
 *   customParams[0].w = glitchAmount (0.0-0.5) - Digital corruption intensity
 *   customParams[1].x = glowStrength (0.5-3.0) - Neon glow intensity
 *   customParams[1].y = smogDensity (0.0-0.5) - Atmospheric haze
 *   customParams[1].z = chromaShift (0.0-15.0) - Chromatic aberration
 *   customParams[1].w = fillOpacity (0.3-0.9) - Inner fill darkness
 *   customColors[0] - Primary color (default #39FF14)
 *   customColors[1] - Secondary color (default #BF00FF)
 */

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
        mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x),
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

// Diagonal circuit grid with animated energy pulses
float diagonalCircuitGrid(vec2 uv, float density, float time) {
    float grid = 0.0;
    float lineWidth = 0.025;

    // Diagonal grid lines (X pattern)
    float diag1 = abs(fract((uv.x + uv.y) * density) - 0.5);  // "/" lines
    float diag2 = abs(fract((uv.x - uv.y) * density) - 0.5);  // "\" lines

    // Base diagonal traces
    float trace1 = smoothstep(lineWidth, lineWidth * 0.3, diag1);
    float trace2 = smoothstep(lineWidth, lineWidth * 0.3, diag2);

    // Intersection nodes (where diagonals cross)
    float node1 = smoothstep(0.08, 0.02, diag1);
    float node2 = smoothstep(0.08, 0.02, diag2);
    float nodes = node1 * node2;  // Only where both lines intersect

    // Animated energy pulses traveling along diagonals
    float pulse1A = sin((uv.x + uv.y) * density * 6.28 - time * 3.0) * 0.5 + 0.5;
    float pulse1B = sin((uv.x + uv.y) * density * 6.28 + time * 2.5) * 0.5 + 0.5;
    float pulse2A = sin((uv.x - uv.y) * density * 6.28 - time * 2.8) * 0.5 + 0.5;
    float pulse2B = sin((uv.x - uv.y) * density * 6.28 + time * 3.2) * 0.5 + 0.5;

    // Sharpen pulses
    pulse1A = pow(pulse1A, 4.0);
    pulse1B = pow(pulse1B, 4.0);
    pulse2A = pow(pulse2A, 4.0);
    pulse2B = pow(pulse2B, 4.0);

    // Combine traces with pulses
    grid += trace1 * (0.3 + (pulse1A + pulse1B) * 0.5);
    grid += trace2 * (0.3 + (pulse2A + pulse2B) * 0.5);

    // Bright nodes at intersections
    grid += nodes * 1.5;

    // Random segment visibility for variation (not position-based, time-based)
    float segmentId1 = floor((uv.x + uv.y) * density);
    float segmentId2 = floor((uv.x - uv.y) * density);
    float flicker1 = step(0.3, fract(sin(segmentId1 * 127.1 + floor(time * 0.5)) * 43758.5));
    float flicker2 = step(0.3, fract(sin(segmentId2 * 311.7 + floor(time * 0.5)) * 43758.5));

    // Apply flicker to make some segments dimmer
    grid *= 0.6 + 0.4 * max(flicker1, flicker2);

    return clamp(grid, 0.0, 1.0);
}

// Circuit trace pattern - creates glowing digital pathways
float circuitPattern(vec2 uv, float density, float time) {
    float circuit = 0.0;

    // Horizontal traces
    float hLine = abs(fract(uv.y * density) - 0.5);
    float hTrace = smoothstep(0.02, 0.0, hLine);

    // Vertical traces
    float vLine = abs(fract(uv.x * density) - 0.5);
    float vTrace = smoothstep(0.02, 0.0, vLine);

    // Junction nodes where traces meet
    float node = length(fract(uv * density) - 0.5);
    float nodeGlow = smoothstep(0.15, 0.05, node);

    // Animated energy pulses along traces
    float hPulse = sin(uv.x * 30.0 - time * 4.0) * 0.5 + 0.5;
    float vPulse = sin(uv.y * 30.0 + time * 3.0) * 0.5 + 0.5;

    // Selective trace visibility based on noise
    float traceNoise = noise(uv * density * 0.5 + time * 0.1);
    float traceVisible = step(0.3, traceNoise);

    circuit = (hTrace * hPulse + vTrace * vPulse) * traceVisible;
    circuit += nodeGlow * 1.5;

    // Diagonal circuit grid overlay
    float diagGrid = diagonalCircuitGrid(uv, density * 0.5, time);
    circuit += diagGrid * 0.5;

    return clamp(circuit, 0.0, 1.0);
}

// Toxic drip effect - viscous poison dripping down
float toxicDrip(vec2 uv, float time, float intensity) {
    float drip = 0.0;

    // Multiple drip streams
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float xOffset = hash11(fi * 17.3) * 0.8 + 0.1;
        float speed = 0.3 + hash11(fi * 23.7) * 0.4;
        float phase = hash11(fi * 31.1) * 6.28;

        // Drip position
        float dripX = xOffset + sin(time * 0.5 + phase) * 0.05;
        float dripY = fract(time * speed + hash11(fi * 41.3));

        // Drip shape - elongated teardrop
        vec2 dripPos = vec2(dripX, 1.0 - dripY);
        vec2 toDrip = uv - dripPos;

        // Stretch vertically for drip shape
        toDrip.y *= 0.3;
        float dist = length(toDrip);

        // Drip with trail
        float dripHead = smoothstep(0.04, 0.0, dist);
        float trail = smoothstep(0.02, 0.0, abs(uv.x - dripX)) *
                      smoothstep(dripPos.y, dripPos.y + 0.3, uv.y) *
                      smoothstep(1.0, dripPos.y + 0.1, uv.y);

        drip += (dripHead + trail * 0.5) * (0.5 + 0.5 * sin(time * 3.0 + phase));
    }

    return drip * intensity;
}

// Digital glitch/corruption effect
vec2 glitchOffset(vec2 uv, float time, float amount) {
    float glitchTime = floor(time * 20.0);
    float trigger = hash11(glitchTime);

    vec2 offset = vec2(0.0);

    if (trigger > 0.85 && amount > 0.01) {
        float band = hash11(glitchTime + 100.0);
        float bandHeight = 0.03 + hash11(glitchTime + 200.0) * 0.08;

        if (abs(uv.y - band) < bandHeight) {
            offset.x = (hash11(glitchTime + 300.0) - 0.5) * amount * 0.2;

            // RGB split simulation prep
            float split = hash11(glitchTime + 400.0);
            if (split > 0.7) {
                offset.x *= 2.0;
            }
        }
    }

    return offset;
}

// Smog/atmospheric haze
float atmosphericSmog(vec2 uv, float time, float density) {
    float smog = 0.0;

    // Layered noise for volumetric feel
    smog += fbm(uv * 3.0 + vec2(time * 0.1, time * 0.05), 4) * 0.6;
    smog += fbm(uv * 6.0 - vec2(time * 0.15, 0.0), 3) * 0.3;
    smog += fbm(uv * 1.5 + vec2(0.0, time * 0.08), 3) * 0.4;

    // Swirling motion
    float swirl = sin(uv.x * 4.0 + time * 0.3) * cos(uv.y * 3.0 - time * 0.2) * 0.2;
    smog += swirl;

    return clamp(smog * density, 0.0, 1.0);
}

vec4 renderToxicCircuitZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = max(params.x, 6.0);
    float borderWidth = max(params.y, 2.5);

    // Parameters with defaults (toned down for subtlety)
    float circuitDensity = customParams[0].x > 0.5 ? customParams[0].x : 12.0;
    float pulseSpeed = customParams[0].y > 0.1 ? customParams[0].y : 1.0;
    float dripIntensity = customParams[0].z > 0.001 ? customParams[0].z : 0.2;
    float glitchAmount = customParams[0].w > 0.001 ? customParams[0].w : 0.08;
    float glowStrength = customParams[1].x > 0.1 ? customParams[1].x : 1.0;
    float smogDensity = customParams[1].y > 0.001 ? customParams[1].y : 0.1;
    float chromaShift = customParams[1].z > 0.1 ? customParams[1].z : 3.0;
    float fillOpacity = customParams[1].w > 0.1 ? customParams[1].w : 0.7;
    float edgeGlowStrength = customParams[2].x > 0.001 ? customParams[2].x : 0.35;
    float mouseInfluenceStrength = customParams[2].y > 0.01 ? customParams[2].y : 1.5;

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;

    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Colors - Toxic Circuit palette
    vec3 primaryColor = colorWithFallback(customColors[0].rgb, vec3(0.224, 1.0, 0.078));   // #39FF14
    vec3 secondaryColor = colorWithFallback(customColors[1].rgb, vec3(0.749, 0.0, 1.0)); // #BF00FF
    vec3 bgColor = vec3(0.102, 0.0, 0.2);                                                // #1A0033
    vec3 accentColor = vec3(1.0, 0.063, 0.941);                                          // #FF10F0

    float time = iTime * pulseSpeed;

    // Highlighted state - dramatic color swap and intensity boost
    float highlightBoost = 1.0;
    if (isHighlighted) {
        // Swap primary colors - purple becomes dominant, green becomes accent
        vec3 temp = primaryColor;
        primaryColor = accentColor;
        secondaryColor = mix(temp, vec3(1.0, 1.0, 0.0), 0.3);  // Yellow-green accent
        bgColor = vec3(0.15, 0.0, 0.25);  // Lighter purple base

        // Dramatic parameter boosts
        glowStrength *= 2.2;
        chromaShift *= 1.8;
        dripIntensity *= 2.0;
        circuitDensity *= 0.8;  // Slightly larger circuits
        pulseSpeed *= 1.5;  // Faster animation
        fillOpacity = min(fillOpacity + 0.15, 0.95);
        highlightBoost = 1.4;  // Overall brightness multiplier
    }

    vec4 result = vec4(0.0);
    vec2 localUV = (fragCoord - rectPos) / rectSize;

    // Mouse position in local UV space (0-1 within this zone)
    vec2 mousePixel = iMouse.xy;
    vec2 mouseLocal = zoneLocalUV(mousePixel, rectPos, rectSize);
    float mouseInZone = step(0.0, mouseLocal.x) * step(mouseLocal.x, 1.0) *
                        step(0.0, mouseLocal.y) * step(mouseLocal.y, 1.0);

    // Distance from current pixel to mouse (in local UV space)
    float mouseDist = length(localUV - mouseLocal);

    // === MOUSE INFLUENCE CALCULATION ===
    float mouseInfluence = 0.0;
    float mouseWarpInfluence = 0.0;
    vec2 mouseDir = vec2(0.0);

    if (mouseInZone > 0.5) {
        // Smooth falloff from cursor
        mouseInfluence = smoothstep(0.4, 0.0, mouseDist);
        mouseInfluence = mouseInfluence * mouseInfluence;  // Smoother curve

        // Tighter falloff for UV warp
        mouseWarpInfluence = smoothstep(0.25, 0.0, mouseDist);
        mouseWarpInfluence = pow(mouseWarpInfluence, 1.5);

        // Direction from pixel to mouse
        mouseDir = mouseDist > 0.001 ? normalize(mouseLocal - localUV) : vec2(0.0);
    }

    // === UV DISTORTION FROM MOUSE ===
    vec2 warpedUV = localUV;
    if (mouseWarpInfluence > 0.001) {
        // Swirl: rotate UVs around cursor
        vec2 delta = localUV - mouseLocal;
        float swirlAngle = mouseWarpInfluence * 0.4 * sin(iTime * 1.2);
        float s = sin(swirlAngle);
        float c = cos(swirlAngle);
        vec2 rotated = vec2(delta.x * c - delta.y * s, delta.x * s + delta.y * c);

        // Pinch: pull UVs toward cursor
        float pinch = mouseWarpInfluence * 0.06;
        rotated *= (1.0 - pinch);

        warpedUV = mouseLocal + rotated;
    }

    // Apply glitch offset to warped UV
    vec2 glitchOff = glitchOffset(warpedUV, iTime, glitchAmount);
    vec2 glitchedUV = warpedUV + glitchOff;

    // === MOUSE-ACCELERATED TIME ===
    // Animations speed up near cursor
    float timeBoost = 1.0 + mouseInfluence * mouseInfluenceStrength;
    float mouseTime = time * timeBoost;

    // Inside the zone
    if (d < 0.0) {
        // Dark toxic base with depth
        float depthFade = 1.0 - length(localUV - 0.5) * 0.3;
        vec3 baseColor = bgColor * depthFade * 0.8;

        // Circuit trace overlay (uses accelerated time near mouse)
        float circuit = circuitPattern(glitchedUV, circuitDensity, mouseTime);

        // Color the circuits with animated gradient
        float colorMix = sin(mouseTime * 0.5 + localUV.x * 3.0) * 0.5 + 0.5;
        vec3 circuitColor = mix(primaryColor, secondaryColor, colorMix);

        // Chromatic aberration - approximate from single evaluation
        // Instead of calling circuitPattern 3 times, shift the single result
        // by sampling the UV gradient to fake the R/B channel offsets
        float enhancedChroma = chromaShift * (1.0 + mouseInfluence * 2.5);
        vec2 chromaDir = vec2(enhancedChroma / rectSize.x, 0.0);
        // Use dFdx to estimate how circuit changes per pixel in the chroma direction
        float dCircuit = dFdx(circuit) * chromaDir.x * rectSize.x;
        float circuitR = clamp(circuit + dCircuit, 0.0, 1.0);
        float circuitB = clamp(circuit - dCircuit, 0.0, 1.0);

        vec3 circuitRGB;
        circuitRGB.r = circuitR * circuitColor.r;
        circuitRGB.g = circuit * circuitColor.g;
        circuitRGB.b = circuitB * circuitColor.b;

        baseColor += circuitRGB * glowStrength;

        // Toxic drip overlay - drips attracted toward cursor
        vec2 dripUV = glitchedUV;
        if (mouseInfluence > 0.01) {
            dripUV += mouseDir * mouseInfluence * 0.08;  // Drips bend toward cursor
        }
        float drip = toxicDrip(dripUV, iTime * timeBoost, dripIntensity * (1.0 + mouseInfluence));
        vec3 dripColor = mix(primaryColor, accentColor, drip * 0.3 + mouseInfluence * 0.2);
        baseColor += dripColor * drip * 0.8;

        // Atmospheric smog layer - clears near cursor
        float smogMod = smogDensity * (1.0 - mouseInfluence * 0.6);  // Smog thins near cursor
        float smog = atmosphericSmog(localUV, iTime, smogMod);
        vec3 smogColor = mix(bgColor, secondaryColor * 0.3, smog);
        baseColor = mix(baseColor, smogColor, smog * 0.5);

        // Iridescent shimmer (like toxic skin) - subtle
        float shimmer = sin(localUV.x * 40.0 + localUV.y * 30.0 + time * 2.0) * 0.5 + 0.5;
        shimmer = pow(shimmer, 4.0);
        vec3 shimmerColor = mix(primaryColor, secondaryColor, shimmer);
        baseColor += shimmerColor * shimmer * 0.08;

        // Energy pulse wave from center - subtle
        float pulseWave = sin(length(localUV - 0.5) * 15.0 - time * 2.0) * 0.5 + 0.5;
        pulseWave = pow(pulseWave, 5.0);
        baseColor += primaryColor * pulseWave * 0.1;

        // Inner edge glow (fresnel-like) - toned down
        float edgeDist = -d / 50.0;
        float fresnel = pow(1.0 - clamp(edgeDist, 0.0, 1.0), 3.0);
        baseColor += mix(primaryColor, secondaryColor, fresnel) * fresnel * edgeGlowStrength;

        // === MOUSE INTERACTION - Integrated Effects ===
        if (mouseInfluence > 0.001) {
            // Boost circuit brightness near cursor
            baseColor += circuitColor * circuit * mouseInfluence * 0.8;

            // Color shift toward neon magenta near cursor
            baseColor = mix(baseColor, baseColor * 1.3 + accentColor * 0.15, mouseInfluence * 0.6);

            // Cursor hotspot glow
            float hotspot = smoothstep(0.06, 0.0, mouseDist);
            float pulse = sin(iTime * 3.0) * 0.3 + 0.7;
            baseColor += accentColor * hotspot * pulse * 0.6;
        }

        // Energy ripple emanating from cursor - extends beyond main influence zone
        if (mouseInZone > 0.5 && mouseDist < 0.6) {
            float rippleFade = smoothstep(0.6, 0.0, mouseDist);
            float ripple = sin(mouseDist * 40.0 - iTime * 5.0) * 0.5 + 0.5;
            ripple = pow(ripple, 2.0) * rippleFade;
            baseColor += mix(primaryColor, secondaryColor, 0.5) * ripple * 0.35;
        }

        // Flicker
        float flicker = 0.92 + 0.08 * sin(iTime * 17.0) * sin(iTime * 23.0);
        baseColor *= flicker;

        // Apply highlight brightness boost
        baseColor *= highlightBoost;

        // Highlighted: add pulsing inner bloom
        if (isHighlighted) {
            float bloom = sin(iTime * 4.0) * 0.15 + 0.85;
            baseColor *= bloom;
            baseColor += accentColor * 0.15;
        }

        result.rgb = baseColor;
        result.a = fillOpacity;
    }

    // Neon toxic border with chromatic split
    float effectiveBorderWidth = isHighlighted ? borderWidth * 1.5 : borderWidth;
    float border = softBorder(d, effectiveBorderWidth);
    if (abs(d) < effectiveBorderWidth + chromaShift + 5.0) {
        // Chromatic border split
        float chromaAmount = isHighlighted ? chromaShift * 0.6 : chromaShift * 0.4;
        float dR = sdRoundedBox(p + vec2(chromaAmount, 0.0), rectSize * 0.5, borderRadius);
        float dB = sdRoundedBox(p - vec2(chromaAmount, 0.0), rectSize * 0.5, borderRadius);

        float bR = softBorder(dR, effectiveBorderWidth);
        float bG = border;
        float bB = softBorder(dB, effectiveBorderWidth);

        // Animated border color - faster and brighter when highlighted
        float pulseRate = isHighlighted ? 5.0 : 3.0;
        float borderPulse = sin(iTime * pulseSpeed * pulseRate) * 0.5 + 0.5;
        vec3 borderBase = mix(primaryColor, secondaryColor, borderPulse);

        // Highlighted: use brighter, more saturated colors
        if (isHighlighted) {
            borderBase = mix(borderBase, accentColor, 0.4);
            borderBase *= 1.3;
        }

        vec3 borderRGB;
        float intensityMult = isHighlighted ? 2.8 : 2.0;
        borderRGB.r = borderBase.r * bR * intensityMult;
        borderRGB.g = borderBase.g * bG * intensityMult;
        borderRGB.b = borderBase.b * bB * intensityMult;

        // White-hot core - brighter when highlighted
        float coreIntensity = pow(border, 2.0);
        float whiteMix = isHighlighted ? 0.8 : 0.6;
        borderRGB = mix(borderRGB, vec3(1.0), coreIntensity * whiteMix);

        // Traveling energy along border - faster when highlighted
        float angle = atan(p.y, p.x);
        float travelSpeed = isHighlighted ? 8.0 : 4.0;
        float energyTravel = sin(angle * 8.0 - iTime * travelSpeed) * 0.5 + 0.5;
        energyTravel = pow(energyTravel, 3.0);
        borderRGB += primaryColor * energyTravel * border * (isHighlighted ? 0.8 : 0.5);

        // Mouse intensifies nearby border section
        if (mouseInfluence > 0.01) {
            borderRGB += accentColor * mouseInfluence * border * 0.5;
            borderRGB *= 1.0 + mouseInfluence * 0.4;
        }

        float borderAlpha = max(max(bR, bG), bB);
        result.rgb = mix(result.rgb, borderRGB, borderAlpha);
        result.a = max(result.a, borderAlpha * 0.98);
    }

    // Outer toxic glow
    float outerGlowRange = isHighlighted ? 60.0 : 25.0;
    if (d > 0.0 && d < outerGlowRange) {
        float glowFalloff1 = isHighlighted ? 20.0 : 10.0;
        float glowFalloff2 = isHighlighted ? 40.0 : 20.0;

        float glow1 = expGlow(d, glowFalloff1, isHighlighted ? 0.7 : 0.3);
        float glow2 = expGlow(d, glowFalloff2, isHighlighted ? 0.4 : 0.15);

        float glowPulse = sin(iTime * pulseSpeed * 2.0) * 0.2 + 0.8;
        vec3 glowColor = mix(primaryColor, secondaryColor, glow1) * glowPulse;

        // Highlighted: add pulsing ring effect
        if (isHighlighted) {
            float ringDist = mod(d - iTime * 25.0, 20.0);
            float ring = smoothstep(2.5, 0.0, ringDist) * smoothstep(0.0, 1.0, ringDist);
            glowColor += accentColor * ring * 1.2;
            glow1 += ring * 0.3;
        }

        result.rgb += glowColor * (glow1 + glow2) * glowStrength * (isHighlighted ? 0.7 : 0.4);
        result.a = max(result.a, (glow1 + glow2 * 0.5) * (isHighlighted ? 0.6 : 0.25));
    }

    // Corner corruption glitches
    if (d < 0.0) {
        vec2 cornerDist = abs(localUV - 0.5) * 2.0;
        float cornerProximity = max(cornerDist.x, cornerDist.y);
        if (cornerProximity > 0.88) {
            float cornerGlitch = hash11(floor(iTime * 15.0) + floor(cornerProximity * 10.0));
            if (cornerGlitch > 0.7) {
                float intensity = (cornerProximity - 0.88) / 0.12;
                result.rgb += accentColor * intensity * 0.8;
            }
        }
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

        vec4 zoneColor = renderToxicCircuitZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);

        color = blendOver(color, zoneColor);
    }

    color = compositeLabelsWithUv(color, fragCoord);

    fragColor = clampFragColor(color);
}
