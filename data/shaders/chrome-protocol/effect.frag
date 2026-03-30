// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

/*
 * CHROME PROTOCOL — Weapon Targeting HUD
 * Inspired by VIPER / Chrome Protocol album
 * "They didn't raise her. They compiled her."
 *
 * Not a circuit board — a weapon's eye view. Rotating acquisition rings,
 * frequency analyzer bars, threat scanlines, and ghost data fragments
 * bleeding through the sterile surface.
 */

// ═══════════════════════════════════════════════════════════════════════
// Noise
// ═══════════════════════════════════════════════════════════════════════

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
    mat2 rot = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < octaves; i++) {
        f += amp * noise(p);
        p = rot * p * 2.0;
        amp *= 0.5;
    }
    return f;
}

// ═══════════════════════════════════════════════════════════════════════
// Acquisition rings: rotating concentric targeting with glow, data arcs,
// traveling energy pulses, and bass-reactive expansion
// ═══════════════════════════════════════════════════════════════════════

// Single ring with inner glow, tick marks, and traveling pulse
float acquisitionRing(vec2 uv, vec2 center, float radius, float width,
                      int ticks, float rotation, float activeFrac, float t) {
    vec2 delta = uv - center;
    float dist = length(delta);
    float angle = atan(delta.y, delta.x);

    // Ring body with soft glow falloff (not just a hard line)
    float ringDist = abs(dist - radius);
    float ring = smoothstep(width, width * 0.15, ringDist);
    float ringGlow = exp(-ringDist / (width * 4.0)) * 0.4; // Soft bloom around ring

    // Tick marks — extend outward from ring
    float tickSpacing = TAU / float(ticks);
    float tickAngle = mod(angle + rotation, tickSpacing);
    float tickMark = smoothstep(0.02, 0.005, tickAngle)
                   + smoothstep(tickSpacing - 0.02, tickSpacing - 0.005, tickAngle);
    float tickRadial = smoothstep(radius + width * 5.0, radius + width, dist)
                     * smoothstep(radius - width * 5.0, radius - width, dist);
    tickMark *= tickRadial;

    // Major ticks (every 4th) are brighter and longer
    float majorTickId = mod(floor((angle + rotation) / tickSpacing), 4.0);
    float majorTick = step(0.5, 1.0 - majorTickId / 4.0); // every 4th
    tickMark *= 0.5 + majorTick * 0.5;

    // Segmented activation with smooth edges (not hard step)
    float segAngle = mod(angle + rotation * 0.5, TAU);
    float segActive = smoothstep(activeFrac * TAU + 0.1, activeFrac * TAU, segAngle);
    // Gap pattern — some segments off for mechanical feel
    float gapId = floor(segAngle / tickSpacing);
    float gapOn = step(0.25, fract(sin(gapId * 127.1 + floor(t * 0.4)) * 43758.5));

    // Traveling energy pulse around the ring circumference
    float pulseAngle = mod(angle - t * 2.0, TAU);
    float pulse = exp(-pulseAngle * 1.5) * 0.8; // Bright head, exponential tail
    float pulseAngle2 = mod(angle + t * 1.5 + PI, TAU);
    float pulse2 = exp(-pulseAngle2 * 2.0) * 0.5; // Counter-rotating dimmer pulse

    float ringResult = ring * gapOn * segActive + ringGlow * segActive;
    ringResult += tickMark * 0.7;
    ringResult += (pulse + pulse2) * ring * 0.6;

    return ringResult;
}

// Data arc: thin partial arc with readout hash (fills space between rings)
float dataArc(vec2 uv, vec2 center, float radius, float startAngle,
              float arcLen, float t) {
    vec2 delta = uv - center;
    float dist = length(delta);
    float angle = atan(delta.y, delta.x);

    float ringDist = abs(dist - radius);
    float arc = smoothstep(0.001, 0.0003, ringDist);

    // Arc mask: only visible within the arc span
    float a = mod(angle - startAngle, TAU);
    float arcMask = smoothstep(0.0, 0.05, a) * smoothstep(arcLen, arcLen - 0.05, a);

    // Data readout: hash-based brightness variation along the arc
    float dataId = floor(a * 20.0 + t);
    float dataOn = step(0.3, fract(sin(dataId * 43.7) * 43758.5));

    return arc * arcMask * (0.3 + dataOn * 0.7);
}

float targetingSystem(vec2 uv, vec2 center, float scale, float t,
                      float bassLvl, float midsLvl, float audioReact) {
    float result = 0.0;

    // Bass expands rings slightly (breathing effect)
    float breathe = 1.0 + bassLvl * audioReact * 0.08;

    // Outer ring — slow rotation, bass responsive
    float outerR = 0.38 * scale * breathe;
    float outerActive = 0.6 + bassLvl * audioReact * 0.4;
    result += acquisitionRing(uv, center, outerR, 0.002, 24,
                              t * 0.3, outerActive, t) * 0.6;

    // Middle ring — counter-rotation, mids responsive
    float midR = 0.28 * scale * breathe;
    float midActive = 0.5 + midsLvl * audioReact * 0.5;
    result += acquisitionRing(uv, center, midR, 0.0018, 16,
                              -t * 0.5, midActive, t) * 0.5;

    // Inner ring — fast rotation
    float innerR = 0.15 * scale * breathe;
    result += acquisitionRing(uv, center, innerR, 0.0012, 8,
                              t * 0.8, 0.85, t) * 0.4;

    // Data arcs between rings (rotating readout displays)
    result += dataArc(uv, center, (outerR + midR) * 0.5, t * 0.2, 1.5, t * 2.0) * 0.25;
    result += dataArc(uv, center, (midR + innerR) * 0.5, -t * 0.35 + PI, 1.2, t * 1.5) * 0.2;

    // Radial scan lines (like radar sweep spokes)
    vec2 cd = uv - center;
    float spokeA = atan(cd.y, cd.x);
    float spoke1 = exp(-mod(spokeA - t * 0.6, TAU) * 3.0) * 0.2;
    float spoke2 = exp(-mod(spokeA + t * 0.4 + PI * 0.5, TAU) * 4.0) * 0.15;
    float spokeMask = smoothstep(innerR * 0.5, innerR, length(cd))
                    * smoothstep(outerR + 0.02, outerR, length(cd));
    result += (spoke1 + spoke2) * spokeMask;

    // Center crosshair with animated dashes
    vec2 absCD = abs(cd);
    float dashH = step(mod(absCD.x * 80.0 + t * 3.0, 2.0), 1.2); // dashed line
    float dashV = step(mod(absCD.y * 80.0 - t * 2.0, 2.0), 1.2);
    float crossH = step(absCD.y, 0.001) * smoothstep(0.08 * scale, 0.015 * scale, absCD.x) * dashH;
    float crossV = step(absCD.x, 0.001) * smoothstep(0.08 * scale, 0.015 * scale, absCD.y) * dashV;
    float centerGap = smoothstep(0.012 * scale, 0.018 * scale, length(cd));
    result += (crossH + crossV) * 0.35 * centerGap;

    // Center dot with pulse
    float dotPulse = 0.7 + 0.3 * sin(t * 4.0);
    result += smoothstep(0.006 * scale, 0.002 * scale, length(cd)) * 0.5 * dotPulse;

    return clamp(result, 0.0, 1.0);
}

// ═══════════════════════════════════════════════════════════════════════
// Frequency analyzer: vertical bars responding to audio spectrum
// Positioned along zone edges like a HUD readout
// ═══════════════════════════════════════════════════════════════════════

float frequencyBars(vec2 uv, float t, float sensitivity) {
    if (iAudioSpectrumSize <= 0) return 0.0;

    float result = 0.0;
    int barCount = min(iAudioSpectrumSize, 32);
    float barWidth = 1.0 / float(barCount);

    // Bottom-edge analyzer
    for (int i = 0; i < barCount; i++) {
        float barX = (float(i) + 0.5) * barWidth;
        float barVal = audioBar(i) * sensitivity;
        float xDist = abs(uv.x - barX);
        if (xDist > barWidth * 0.6) continue;

        float barMask = smoothstep(barWidth * 0.45, barWidth * 0.35, xDist);
        float barHeight = barVal * 0.15;
        float yMask = smoothstep(0.0, 0.005, uv.y) * (1.0 - smoothstep(barHeight, barHeight + 0.005, uv.y));

        // Peak hold: bright tip
        float peak = smoothstep(barHeight - 0.008, barHeight - 0.003, uv.y) * yMask;

        result += barMask * yMask * 0.6 + peak * 0.4;
    }

    // Right-edge analyzer (rotated — reads top to bottom)
    for (int i = 0; i < barCount; i++) {
        float barY = (float(i) + 0.5) * barWidth;
        float barVal = audioBar(i) * sensitivity;
        float yDist = abs(uv.y - barY);
        if (yDist > barWidth * 0.6) continue;

        float barMask = smoothstep(barWidth * 0.45, barWidth * 0.35, yDist);
        float barExtent = barVal * 0.1;
        float xMask = smoothstep(1.0, 1.0 - 0.005, uv.x) * (1.0 - smoothstep(1.0 - barExtent, 1.0 - barExtent - 0.005, uv.x));

        result += barMask * xMask * 0.4;
    }

    return clamp(result, 0.0, 1.0);
}

// ═══════════════════════════════════════════════════════════════════════
// Threat scanline: horizontal sweep with data readout trail
// ═══════════════════════════════════════════════════════════════════════

float threatScan(vec2 uv, float t, float speed) {
    float scanY = fract(t * speed * 0.2);
    float dist = abs(uv.y - scanY);

    // Sharp leading edge
    float beam = smoothstep(0.003, 0.0005, dist);

    // Data readout trail: hash-based segments behind the scan line
    float trailMask = step(uv.y, scanY) * smoothstep(scanY - 0.15, scanY, uv.y);
    float trailData = step(0.5, hash21(floor(uv * vec2(80.0, 200.0) + t * 0.5)));
    float trail = trailMask * trailData * 0.15;

    // Horizontal data fragments near scan line
    float nearScan = smoothstep(0.03, 0.0, dist);
    float dataFrag = step(0.7, hash21(floor(vec2(uv.x * 40.0, t * 3.0))));
    float fragments = nearScan * dataFrag * 0.3;

    return beam + trail + fragments;
}

// ═══════════════════════════════════════════════════════════════════════
// Ghost data: suppressed organic memories bleeding through
// Domain-warped noise that breaks through the sterile surface
// ═══════════════════════════════════════════════════════════════════════

float ghostData(vec2 uv, float t, float intensity) {
    if (intensity <= 0.0) return 0.0;

    // Organic noise — everything VIPER suppresses
    vec2 warp = vec2(
        fbm(uv * 3.0 + t * 0.08, 4),
        fbm(uv * 3.0 + vec2(5.2, 1.3) - t * 0.06, 4)
    );
    float organic = fbm(uv * 2.0 + warp * 1.5, 5);

    // Ridged: vein-like fragments (memories of something alive)
    float ridge = 1.0 - abs(organic * 2.0 - 1.0);
    ridge = pow(ridge, 4.0);

    // Appear in glitchy bursts — not constant
    float burstTime = floor(t * 0.7);
    float burst = step(0.75, hash11(burstTime));
    float burstFade = exp(-fract(t * 0.7) * 3.0);

    // Spatial mask: ghosts only appear in certain regions (not everywhere)
    float spatialMask = step(0.6, noise(uv * 4.0 + burstTime));

    return ridge * burst * burstFade * spatialMask * intensity;
}

// ═══════════════════════════════════════════════════════════════════════
// Hexagonal depth pattern
// ═══════════════════════════════════════════════════════════════════════

float hexGrid(vec2 uv, float scale) {
    vec2 p = uv * scale;
    vec2 h = vec2(p.x + p.y * 0.577350269, p.y * 1.154700538);
    vec2 a = mod(h, 2.0) - 1.0;
    vec2 b = mod(h + 1.0, 2.0) - 1.0;
    float d = min(dot(a, a), dot(b, b));
    return smoothstep(0.92, 0.85, d);
}

// ═══════════════════════════════════════════════════════════════════════
// Glitch displacement
// ═══════════════════════════════════════════════════════════════════════

vec2 glitchOffset(vec2 uv, float time, float amount) {
    float glitchTime = floor(time * 20.0);
    float trigger = hash11(glitchTime);
    vec2 offset = vec2(0.0);
    if (trigger > 0.85 && amount > 0.01) {
        float band = hash11(glitchTime + 100.0);
        float bandHeight = 0.02 + hash11(glitchTime + 200.0) * 0.06;
        if (abs(uv.y - band) < bandHeight) {
            offset.x = (hash11(glitchTime + 300.0) - 0.5) * amount * 0.15;
            if (hash11(glitchTime + 400.0) > 0.7) offset.x *= 2.0;
        }
    }
    return offset;
}

// ═══════════════════════════════════════════════════════════════════════
// HUD decorations: corner brackets, status indicators
// ═══════════════════════════════════════════════════════════════════════

float bracketCorner(vec2 uv, vec2 c, vec2 dir, float len, float thick) {
    float hArm = step(abs(uv.y - c.y), thick)
                * step(0.0, (uv.x - c.x) * dir.x)
                * step((uv.x - c.x) * dir.x, len);
    float vArm = step(abs(uv.x - c.x), thick)
                * step(0.0, (uv.y - c.y) * dir.y)
                * step((uv.y - c.y) * dir.y, len);
    return hArm + vArm;
}

float hudBrackets(vec2 uv, float borderInset) {
    float len = 0.08;
    float thick = 0.0015;
    float bi = borderInset;
    float bo = 1.0 - borderInset;

    float result = bracketCorner(uv, vec2(bi, bi), vec2( 1.0,  1.0), len, thick)
                 + bracketCorner(uv, vec2(bo, bi), vec2(-1.0,  1.0), len, thick)
                 + bracketCorner(uv, vec2(bi, bo), vec2( 1.0, -1.0), len, thick)
                 + bracketCorner(uv, vec2(bo, bo), vec2(-1.0, -1.0), len, thick);

    return clamp(result, 0.0, 1.0);
}

// Status pips: small dots along top edge
float statusPips(vec2 uv, float t, int count, float bass) {
    float result = 0.0;
    float spacing = 0.8 / float(max(count, 1));
    float startX = 0.1;

    for (int i = 0; i < count && i < 12; i++) {
        float pipX = startX + float(i) * spacing;
        float pipY = 0.03;
        float d = length(uv - vec2(pipX, pipY));

        // Active/inactive based on audio level
        float threshold = float(i) / float(count);
        float lit = step(threshold, bass * 1.5);
        float brightness = lit * 0.8 + (1.0 - lit) * 0.15;

        result += smoothstep(0.005, 0.002, d) * brightness;
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════
// Zone rendering
// ═══════════════════════════════════════════════════════════════════════

vec4 renderChromeZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                      vec4 params, bool isHighlighted,
                      float bass, float mids, float treble, float overall, bool hasAudio) {
    float borderRadius = max(params.x, 6.0);
    float borderWidth = max(params.y, 2.5);

    // Parameters
    float ringScale      = customParams[0].x;
    float animSpeed      = customParams[0].y;
    float hudBright      = customParams[0].z;
    // customParams[0].w (ringThickness) — reserved for future use
    float scanSpeed      = customParams[1].x;
    // customParams[1].y (scanWidth) — reserved for future use
    float scanIntensity  = customParams[1].z;
    float fillOpacity    = customParams[1].w;
    float glowStr        = customParams[2].x;
    int   particleCount  = int(customParams[2].y); // repurposed: status pip count
    float audioReact     = customParams[2].z;
    float glitchIntensity= customParams[2].w;
    float edgeGlow       = customParams[3].w;
    float ghostIntensity = customParams[4].x; // was hexOverlay
    float particleSpeed  = customParams[4].z; // repurposed: ghost data speed
    float surgeThreshold = customParams[4].w;
    float mouseInfluenceStr = customParams[5].x;
    float noiseFloor     = customParams[5].y;

    vec3 chromeCol = colorWithFallback(customColors[0].rgb, vec3(0.753, 0.816, 0.878));
    vec3 dataCol   = colorWithFallback(customColors[1].rgb, vec3(0.0, 0.706, 1.0));
    vec3 scanCol   = colorWithFallback(customColors[2].rgb, vec3(0.91, 0.957, 1.0));
    vec3 bgCol     = colorWithFallback(customColors[3].rgb, vec3(0.039, 0.055, 0.078));
    // Ghost data uses a warm organic color — the suppressed humanity
    vec3 ghostCol  = vec3(0.8, 0.3, 0.15); // amber/warm — contrast to cold chrome

    vec2 rectPos = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);
    float px = pxScale();

    float vitality = zoneVitality(isHighlighted);
    float time = iTime * animSpeed;

    // Vitality modulation
    chromeCol = vitalityDesaturate(chromeCol, vitality);
    dataCol = vitalityDesaturate(dataCol, vitality);
    glowStr *= vitalityScale(0.5, 2.0, vitality);
    fillOpacity = mix(fillOpacity, min(fillOpacity + 0.12, 0.95), vitality);
    float highlightBoost = vitalityScale(0.75, 1.35, vitality);

    // Audio system
    float audioPulse = hasAudio ? bass * audioReact : 0.0;
    float audioMid = hasAudio ? mids * audioReact : 0.0;
    float audioHigh = hasAudio ? treble * audioReact : 0.0;

    vec4 result = vec4(0.0);
    vec2 localUV = zoneLocalUV(fragCoord, rectPos, rectSize);
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));

    // ── Mouse: targeting lock ────────────────────────────────────────
    vec2 mousePixel = iMouse.xy;
    vec2 mouseLocal = zoneLocalUV(mousePixel, rectPos, rectSize);
    float mouseInZone = step(0.0, mouseLocal.x) * step(mouseLocal.x, 1.0)
                      * step(0.0, mouseLocal.y) * step(mouseLocal.y, 1.0);
    float mouseDist = length(localUV - mouseLocal);
    float mouseInfluence = 0.0;
    if (mouseInZone > 0.5) {
        mouseInfluence = smoothstep(0.4, 0.0, mouseDist);
        mouseInfluence *= mouseInfluence * mouseInfluenceStr;
    }

    // Glitch displacement
    vec2 glitchOff = glitchOffset(localUV, iTime, glitchIntensity * (1.0 + audioHigh * 0.5));
    vec2 glitchedUV = localUV + glitchOff;

    if (d < 0.0) {
        // ── Base: dark clinical void ─────────────────────────────────
        vec3 baseColor = bgCol * 0.8 + fillColor.rgb * 0.15;
        // Subtle radial gradient (darker at edges — depth)
        float vignette = 1.0 - length(localUV - 0.5) * 0.4;
        baseColor *= vignette;

        // ── Hex depth pattern (fills the void between elements) ────────
        float hex = hexGrid(glitchedUV, ringScale * 8.0) * 0.15 * vitality;
        float hexPulse = 0.5 + 0.5 * sin(time * 0.8 + length(localUV - 0.5) * 8.0);
        baseColor += chromeCol * hex * 0.2 * hexPulse;

        // ── Acquisition rings (zone center) ──────────────────────────
        vec2 zoneCenter = vec2(0.5, 0.5);
        float rings = targetingSystem(glitchedUV, zoneCenter, ringScale,
                                      time, audioPulse, audioMid, audioReact);
        // Rings color: chrome → data-blue gradient based on distance from center
        float centerDist = length(glitchedUV - zoneCenter);
        vec3 ringColor = mix(dataCol, chromeCol, smoothstep(0.0, 0.4, centerDist));
        ringColor = mix(ringColor, scanCol, audioPulse * 0.4);
        baseColor += ringColor * rings * hudBright * glowStr * vitality;

        // ── Mouse targeting overlay ──────────────────────────────────
        if (mouseInfluence > 0.01) {
            float mouseRings = targetingSystem(glitchedUV, mouseLocal, ringScale * 0.6,
                                               time * 1.5, audioPulse, audioMid, audioReact);
            baseColor += scanCol * mouseRings * mouseInfluence * 0.8;
            float prox = smoothstep(0.15, 0.0, mouseDist);
            baseColor += dataCol * prox * mouseInfluence * 0.3;
        }

        // ── Frequency analyzer bars ──────────────────────────────────
        float freqBars = frequencyBars(glitchedUV, time, audioReact * 1.5);
        baseColor += dataCol * freqBars * 0.6 * vitality;

        // ── Threat scanline ──────────────────────────────────────────
        float scan = threatScan(glitchedUV, iTime, scanSpeed) * scanIntensity * vitality;
        // Scan beam reveals hex pattern underneath as it passes
        float scanReveal = scan * hex * 0.4;
        baseColor += scanCol * scan * 0.5;
        baseColor += dataCol * scanReveal;

        // ── HUD brackets and status pips ─────────────────────────────
        float brackets = hudBrackets(localUV, 0.03);
        baseColor += chromeCol * brackets * 0.4 * vitality;

        float pips = statusPips(localUV, time, particleCount, hasAudio ? bass : 0.3);
        baseColor += dataCol * pips * 0.6 * vitality;

        // ── Ghost data (suppressed memories) ─────────────────────────
        // Bass hits trigger ghost data (not random — correlated with audio impact)
        float ghostTrigger = hasAudio ? smoothstep(0.4, 0.8, bass) : 0.0;
        float ghostBase = ghostData(glitchedUV, iTime * particleSpeed, ghostIntensity * vitality);
        float ghost = ghostBase * (0.3 + ghostTrigger * 0.7); // Always slightly visible, bass amplifies
        baseColor += ghostCol * ghost * 1.0;
        baseColor = mix(baseColor, ghostCol * 0.3, ghost * 0.2);

        // ── Static noise floor ───────────────────────────────────────
        float staticN = hash21(globalUV * 500.0 + iTime * 100.0) * noiseFloor * vitality;
        baseColor += chromeCol * staticN * 0.2;

        // ── Scanline overlay (CRT/HUD) ───────────────────────────────
        float scanlines = 0.95 + 0.05 * sin(fragCoord.y * 1.5);
        baseColor *= scanlines;

        // ── Chromatic aberration (cold precision splitting under stress) ─
        float chromaStr = (1.0 + audioPulse * 2.0) * vitalityScale(0.5, 1.5, vitality);
        vec2 chromaOff = vec2(chromaStr / max(iResolution.x, 1.0), 0.0);
        float ringsR = targetingSystem(glitchedUV + chromaOff, zoneCenter, ringScale,
                                        time, audioPulse, audioMid, audioReact);
        float ringsB = targetingSystem(glitchedUV - chromaOff, zoneCenter, ringScale,
                                        time, audioPulse, audioMid, audioReact);
        baseColor.r += ringsR * chromeCol.r * 0.06 * glowStr;
        baseColor.b += ringsB * dataCol.b * 0.06 * glowStr;

        // ── Bass surge: radial overload wipe from center ─────────────
        if (hasAudio && audioPulse > surgeThreshold) {
            float surge = (audioPulse - surgeThreshold) / max(1.0 - surgeThreshold, 0.01);
            surge *= surge;
            // Radial wipe: overexposure expands outward from center
            float wipeRadius = surge * 0.6;
            float wipe = smoothstep(wipeRadius, wipeRadius - 0.1, centerDist);
            baseColor += scanCol * wipe * surge * 1.5;
            // Ring segments flash in sequence
            baseColor += rings * mix(dataCol, scanCol, surge) * surge * 1.2;
        }

        // ── Treble: glitch bands ─────────────────────────────────────
        if (hasAudio && treble > 0.15) {
            float bandY = floor(localUV.y * 30.0 + iTime * 15.0);
            float band = step(0.88, hash11(bandY + iTime * 7.0));
            float shift = (hash11(bandY + 100.0) - 0.5) * 0.03 * audioHigh;
            vec2 shiftedUV = localUV + vec2(shift, 0.0);
            float shiftedRings = targetingSystem(shiftedUV, zoneCenter, ringScale,
                                                  time, audioPulse, audioMid, audioReact);
            baseColor = mix(baseColor, dataCol * shiftedRings * 2.0, band * audioHigh * 0.5);
        }

        // ── Flicker ──────────────────────────────────────────────────
        float flicker = 0.95 + 0.05 * sin(iTime * 19.0) * sin(iTime * 27.0);
        if (hasAudio) {
            float standby = smoothstep(0.3, 0.0, overall * audioReact);
            flicker -= standby * 0.15 * max(sin(iTime * 47.0) * sin(iTime * 61.0), 0.0);
        }
        baseColor *= max(flicker, 0.0) * highlightBoost;

        result = vec4(baseColor, fillOpacity * fillColor.a);
    }

    // ── Border ───────────────────────────────────────────────────────
    float borderFactor = softBorder(d, borderWidth);
    if (borderFactor > 0.001) {
        vec3 borderTint = mix(chromeCol, dataCol, 0.3 + audioPulse * 0.2);
        vec3 borderFinal = mix(borderTint, borderColor.rgb, 0.3);
        borderFinal *= (1.0 + audioPulse * 0.4) * glowStr * highlightBoost;
        result = blendOver(result, vec4(borderFinal * borderFactor, borderFactor * borderColor.a));
    }

    // ── Outer glow ───────────────────────────────────────────────────
    if (d > 0.0 && d < 50.0 * px) {
        float rimFalloff = (8.0 + audioPulse * 10.0) * px;
        float rim = exp(-d / rimFalloff) * edgeGlow * vitality;
        vec3 rimCol = mix(chromeCol, dataCol, audioPulse * 0.3);
        result = blendOver(result, vec4(rimCol * rim * highlightBoost, rim * 0.5));
    }

    return result;
}

void main() {
    bool hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    float overall = getOverallSoft();

    vec4 result = vec4(0.0);

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 zone = renderChromeZone(
            vFragCoord, zoneRects[i], zoneFillColors[i], zoneBorderColors[i],
            zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio
        );
        result = blendOver(result, zone);
    }

    // ─── Labels: Targeting Designation HUD ─────────────────────────────
    bool showLabels = customParams[4].y > 0.5;
    if (showLabels) {
        float labelSpread = customParams[3].x;
        float labelBright = customParams[3].y;
        float labelReact  = customParams[3].z;

        vec3 lChromeCol = colorWithFallback(customColors[0].rgb, vec3(0.753, 0.816, 0.878));
        vec3 lDataCol   = colorWithFallback(customColors[1].rgb, vec3(0.0, 0.706, 1.0));
        vec3 lScanCol   = colorWithFallback(customColors[2].rgb, vec3(0.91, 0.957, 1.0));

        vec2 luv = labelsUv(vFragCoord);
        vec2 texPx = 1.0 / max(iResolution, vec2(1.0));
        vec4 labels = texture(uZoneLabels, luv);
        float spread = labelSpread * pxScale();
        float t = iTime * customParams[0].y;
        float bassMod = hasAudio ? bass * labelReact : 0.0;

        // Multi-layer halo with chromatic split
        float haloTight = 0.0, haloWide = 0.0, haloVWide = 0.0;
        float haloR = 0.0, haloG = 0.0, haloB = 0.0;
        vec2 chromOff = vec2(texPx.x * 2.5, 0.0);
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                vec2 off = vec2(float(dx), float(dy)) * texPx;
                float r2 = float(dx * dx + dy * dy);
                float s = texture(uZoneLabels, luv + off * spread).a;
                haloTight += s * exp(-r2 * 0.5);
                haloWide += s * exp(-r2 * 0.2);
                haloVWide += s * exp(-r2 * 0.1);
                haloR += texture(uZoneLabels, luv + off * spread + chromOff).a * exp(-r2 * 0.2);
                haloG += s * exp(-r2 * 0.2);
                haloB += texture(uZoneLabels, luv + off * spread - chromOff).a * exp(-r2 * 0.2);
            }
        }
        haloTight /= 10.0; haloWide /= 16.5; haloVWide /= 22.0;

        // Clinical precision flicker
        float clinFlicker = 0.93 + 0.07 * step(0.5, fract(t * 9.0 + luv.x * 4.0));
        clinFlicker *= (1.0 + bassMod * 0.3);

        if (haloWide > 0.003) {
            float haloEdge = haloWide * (1.0 - labels.a);
            float haloEdgeTight = haloTight * (1.0 - labels.a);
            float haloEdgeVWide = haloVWide * (1.0 - labels.a);

            // Tight core: near-white targeting lock glow
            result.rgb += lScanCol * 1.1 * haloEdgeTight * 0.6 * clinFlicker;

            // Chromatic holographic split (RGB offset channels)
            vec3 chromHalo = vec3(haloR, haloG, haloB) * (1.0 - labels.a);
            vec3 chromCol = chromHalo * lDataCol * 0.45 * clinFlicker;
            chromCol.r *= lChromeCol.r * 1.2;
            chromCol.b *= lDataCol.b * 1.3;
            result.rgb += chromCol;

            // Wide targeting acquisition haze
            result.rgb += lChromeCol * 0.25 * haloEdgeVWide * 0.3 * clinFlicker;

            // Lock-on bracket lines around label (horizontal bars above/below)
            float bracketH = smoothstep(spread * texPx.y * 6.0, spread * texPx.y * 5.5, abs(luv.y - 0.5));
            float bracketMask = haloEdge * (1.0 - labels.a);
            float bracketAnim = smoothstep(0.0, 0.8, fract(t * 0.5)); // animates closed
            float bracketLine = step(abs(haloEdge - 0.3), 0.05) * bracketMask;
            result.rgb += lDataCol * bracketLine * 0.3 * clinFlicker;

            // Scanline interference bands
            float scanLine = step(0.8, fract(vFragCoord.y * 0.5));
            result.rgb += lDataCol * haloEdge * scanLine * 0.12;

            // Treble: data-burst sparks along halo
            if (hasAudio && treble > 0.1) {
                float burstN = noise2D(luv * 55.0 + t * 5.0);
                float burst = smoothstep(0.6, 0.9, burstN) * treble * labelReact * 2.0;
                result.rgb += lScanCol * haloEdge * burst * clinFlicker;
            }

            result.a = max(result.a, haloEdge * 0.5);
        }

        // ── Label text: cold compiled designation readout ─────────────
        if (labels.a > 0.01) {
            // Base: dark data-blue body (not chrome-white — that's boring)
            vec3 textCol = lDataCol * 0.5;

            // Scrolling data columns within characters (vertical binary rain)
            float colId = floor(vFragCoord.x * 0.3);
            float scrollY = fract(vFragCoord.y * 0.08 + t * 1.5 + colId * 0.7);
            float dataCell = step(0.4, hash21(vec2(colId, floor(scrollY * 12.0 + t * 3.0))));
            // Lit cells pop bright, dark cells stay subdued
            textCol = mix(textCol, lDataCol * 1.4, dataCell * 0.5);

            // Horizontal scan line sweeping through text (compile pass)
            float compileScan = fract(t * 0.8);
            float scanDist = abs(fract(vFragCoord.y / max(iResolution.y, 1.0)) - compileScan);
            float compileLine = smoothstep(0.01, 0.002, scanDist);
            textCol = mix(textCol, lScanCol * 1.5, compileLine * 0.5);

            // Edge-detected rim: bright scan-white stencil edge
            float aL = texture(uZoneLabels, luv + vec2(-texPx.x, 0.0)).a;
            float aR = texture(uZoneLabels, luv + vec2( texPx.x, 0.0)).a;
            float aU = texture(uZoneLabels, luv + vec2(0.0, -texPx.y)).a;
            float aD = texture(uZoneLabels, luv + vec2(0.0,  texPx.y)).a;
            float rim = clamp((4.0 * labels.a - aL - aR - aU - aD) * 2.5, 0.0, 1.0);
            // Rim pops bright — chrome edge on data-blue body
            textCol = mix(textCol, lScanCol, rim * 0.7);

            textCol *= labelBright * clinFlicker * (1.0 + bassMod * 0.4);

            // Ghost data: bass hits cause warm amber interference
            float ghostTrigger = hasAudio ? smoothstep(0.5, 0.9, bass) : 0.0;
            float ghostFade = exp(-fract(t * 0.7) * 3.0);
            textCol = mix(textCol, vec3(0.8, 0.3, 0.15) * labelBright, ghostTrigger * ghostFade * 0.3);

            // Treble: horizontal corruption bands (data errors)
            if (hasAudio && treble > 0.1) {
                float corrBand = step(0.85, fract(vFragCoord.y * 0.12 + iTime * 8.0));
                float tMod = treble * labelReact;
                textCol = mix(textCol, lDataCol * labelBright * 1.8, corrBand * tMod * 0.4);
            }

            textCol = textCol / (0.5 + textCol);
            result.rgb = mix(result.rgb, textCol, labels.a);
            result.a = max(result.a, labels.a);
        }
    }

    fragColor = clampFragColor(result);
}
