// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

/*
 * SONIC RIPPLE — Concentric Audio Rings (Full-Screen with Zone Cutout)
 *
 * A SINGLE continuous ring pattern is centered on the screen center.
 * Zones act as windows into the shared pattern — rings and nebula flow
 * seamlessly across zone boundaries.  Low frequencies sit at
 * the core, high frequencies at the edge.  Each ring's brightness and
 * thickness IS the amplitude of its frequency band.
 *
 * The entire pattern slowly rotates and breathes with overall energy.
 * Edge spectrum bars remain zone-local, painting along each zone's walls.
 *
 * Parameters (customParams):
 *   [0].x = reactivity       — audio sensitivity multiplier
 *   [0].y = ringCount        — how many spectrum rings to draw
 *   [0].z = (unused)
 *   [0].w = ringSpeed        — ring expansion speed
 *   [1].x = rotationSpeed    — slow rotation of the pattern
 *   [1].y = idleAnimation    — animation when no audio
 *   [1].z = glowIntensity    — overall glow brightness
 *   [1].w = fillOpacity      — zone fill alpha
 *
 * Colors:
 *   customColors[0] = primary (default: cyan)
 *   customColors[1] = accent  (default: pink)
 *   customColors[2] = bass    (default: orange)
 */



// ─── Per-zone rendering ─────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted,
                float bass, float mids, float treble, float overall, bool hasAudio)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    // Parameters with defaults
    float reactivity    = customParams[0].x >= 0.0 ? customParams[0].x : 1.5;
    float ringCount     = customParams[0].y >= 0.0 ? customParams[0].y : 24.0;
    float ringSpeed     = customParams[0].w >= 0.0 ? customParams[0].w : 1.0;
    float rotSpeed      = customParams[1].x >= 0.0 ? customParams[1].x : 0.15;
    float idleAnim      = customParams[1].y >= 0.0 ? customParams[1].y : 1.0;
    float glowIntensity = customParams[1].z >= 0.0 ? customParams[1].z : 2.0;
    float fillOpacity   = customParams[1].w >= 0.0 ? customParams[1].w : 0.88;

    // Colors
    vec3 primary  = colorWithFallback(customColors[0].rgb, vec3(0.0, 0.8, 1.0));
    vec3 accent   = colorWithFallback(customColors[1].rgb, vec3(1.0, 0.2, 0.6));
    vec3 bassCol  = colorWithFallback(customColors[2].rgb, vec3(1.0, 0.4, 0.0));

    // ── Highlighted vs dormant ──────────────────────────────
    // Highlighted zones are fully alive; non-highlighted are subdued/dormant.
    // A "vitality" factor (0-1) drives all the differences.
    float vitality = isHighlighted ? 1.0 : 0.3;

    if (isHighlighted) {
        // Vivid dual-color palette — keep both colors active
        glowIntensity *= 1.6;
        reactivity *= 1.4;
        rotSpeed *= 3.0;        // spin faster
    } else {
        // Dormant: desaturated, dimmer, slower
        primary = mix(primary, vec3(dot(primary, vec3(0.299, 0.587, 0.114))), 0.5); // desaturate
        accent = mix(accent, vec3(dot(accent, vec3(0.299, 0.587, 0.114))), 0.5);
        bassCol = mix(bassCol, vec3(dot(bassCol, vec3(0.299, 0.587, 0.114))), 0.5);
        glowIntensity *= 0.5;
        reactivity *= 0.6;
        rotSpeed *= 0.5;        // barely moving
        ringCount = max(ringCount * 0.5, 8.0); // fewer rings
    }

    // Zone geometry — KEEP for cutout, border, edge effects
    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;  // KEEP for border/glow angle
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Screen-space coords for the ring pattern
    vec2 screenCenter = iResolution.xy * 0.5;
    vec2 sp = fragCoord - screenCenter;  // offset from SCREEN center
    vec2 globalUV = fragCoord / max(iResolution, vec2(1.0));

    // Mouse — zone-local check, screen-space offsets
    vec2 mouseLocal = zoneLocalUV(iMouse.xy, rectPos, rectSize);
    bool mouseInZone = mouseLocal.x >= 0.0 && mouseLocal.x <= 1.0 &&
                       mouseLocal.y >= 0.0 && mouseLocal.y <= 1.0;
    vec2 mouseScreenP = iMouse.xy - screenCenter;  // mouse offset from screen center

    float energy    = hasAudio ? overall * reactivity : 0.0;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(iTime * 0.8 * PI)) * idleAnim;

    vec4 result = vec4(0.0);

    // ── Zone interior ───────────────────────────────────────

    if (d < 0.0) {
        // Screen-space reference size for consistent ring scaling
        float refSize = min(iResolution.x, iResolution.y) * 0.5;

        // Gravitational lens: subtle warp toward cursor in screen-space
        vec2 lensedSP = sp;
        if (mouseInZone) {
            vec2 toCursor = mouseScreenP - sp;
            float cursorDist = length(toCursor);
            float lensStrength = exp(-cursorDist / (refSize * 0.15)) * refSize * 0.03;
            lensedSP += normalize(toCursor + vec2(0.001)) * lensStrength;
        }

        float r = length(lensedSP) / refSize;

        // Slow rotation of the coordinate space (screen-centered)
        float angle = atan(lensedSP.y, lensedSP.x) + iTime * rotSpeed;

        // Distance from fragment to mouse in screen-space (for secondary rings)
        float mouseFragR = length(sp - mouseScreenP) / refSize;

        // ── Concentric spectrum rings ───────────────────────
        // Map frequency bands to radial distance: bass at center, treble at edge
        float ringBright = 0.0;
        vec3 ringColor = vec3(0.0);

        // Bass envelope: drives ring width for bass-frequency rings
        float bassEnv = hasAudio ? bass : 0.0;

        if (hasAudio) {
            // Each ring corresponds to a frequency band
            int numRings = int(ringCount);
            for (int i = 0; i < numRings && i < 48; i++) {
                float t = float(i) / float(numRings);  // 0 = lowest freq, 1 = highest

                // Ring radius: low freq near center, high freq near edge
                float ringR = 0.08 + t * 0.85;

                // Slight outward drift synced to time
                ringR += fract(iTime * ringSpeed * 0.1) * (0.02 / float(numRings));

                // Get spectrum value for this band
                float specVal = audioBarSmooth(t) * reactivity;

                // Ring thickness scales with amplitude — louder = thicker
                // Highlighted: fat vivid rings; dormant: thin ghost rings
                float baseWidth = mix(0.004, 0.008, vitality) + specVal * mix(0.012, 0.03, vitality);

                // Bass makes bass-frequency rings physically wider/fatter.
                // exp(-t * 6.0) ensures only inner rings (low t) are affected;
                // the boost decays to near-zero by the mid-range.
                float freqWidthBoost = exp(-t * 6.0) * bassEnv * 0.015;
                float thickness = baseWidth + freqWidthBoost;

                // Angular wobble: the ring isn't a perfect circle (seamless around the circle)
                float wobble = angularNoise(angle, 3.0, t * 20.0 + iTime * 0.5) * specVal * 0.03;
                float ringDist = abs(r - ringR - wobble);

                // Ring intensity: gaussian falloff from center of ring
                float ring = exp(-pow(ringDist / thickness, 2.0)) * specVal;

                // Rings brighten near cursor (screen-space distance)
                if (mouseInZone) {
                    float cursorProximity = exp(-length(sp - mouseScreenP) / (refSize * 0.3));
                    ring *= 1.0 + cursorProximity * 0.3;
                }

                // Color: gradient from primary (bass) through accent (treble)
                vec3 col = mix(primary, accent, t);
                // Bass frequencies get the bass color tint
                if (t < 0.25) {
                    col = mix(bassCol, col, t * 4.0);
                }

                ringBright += ring;
                ringColor += col * ring;
            }

            // Normalize accumulated color
            if (ringBright > 0.01) {
                ringColor /= ringBright;
                ringBright = min(ringBright, 2.0);
            }
        }

        // ── Audio-reactive nebula background ────────────────
        // Warped noise field fills the space between and behind rings.
        // Audio drives the warp intensity, color, and flow speed.

        // Nebula UV — flows at its own pace without bass-driven warping
        float nebScale = customParams[3].x >= 0.0 ? customParams[3].x : 8.0;
        vec2 nebUV = globalUV * (nebScale / 3.2);

        // Multi-octave noise for the nebula pattern
        float neb = 0.0;
        float amp = 0.5;
        vec2 octUV = nebUV;
        for (int o = 0; o < 4; o++) {
            neb += noise2D(octUV) * amp;
            octUV *= 2.1;
            amp *= 0.5;
        }

        // Color the nebula: radial gradient from primary to accent, shifted by noise
        float colorPhase = neb + r * 0.5 + iTime * 0.08;
        vec3 nebColor = mix(primary * 0.7, accent * 0.7, sin(colorPhase * PI) * 0.5 + 0.5);
        // Blend in a third hue for variety
        float hue2 = sin(colorPhase * PI * 0.7 + 2.0) * 0.5 + 0.5;
        nebColor = mix(nebColor, mix(primary, bassCol, 0.5) * 0.6, hue2 * 0.3);
        // Tint the core region with bass color when bass is strong
        if (hasAudio) {
            nebColor = mix(nebColor, bassCol, exp(-r * 2.5) * bass * 0.7);
        }

        // Brightness: always visible base + strong audio boost
        // Highlighted: rich and vivid; dormant: dim fog
        float nebBase = mix(0.08, 0.25, vitality) + idlePulse * 0.15;
        float nebAudio = energy * mix(0.1, 0.4, vitality);
        float nebBright = neb * (nebBase + nebAudio);
        nebBright *= 1.0 - smoothstep(0.0, 1.3, r) * 0.3; // slight fade toward edges

        // ── Compose interior ────────────────────────────────

        // Nebula as the base layer
        result.rgb = nebColor * nebBright;

        // Add spectrum rings on top
        result.rgb += ringColor * ringBright * glowIntensity * 0.6;

        // Overall energy brightens the core
        float coreBright = exp(-r * 2.5) * energy * 0.15;
        result.rgb += primary * coreBright;

        // Treble shimmer: fast angular noise near the edges
        if (hasAudio && treble > 0.05) {
            float edgeFactor = smoothstep(0.5, 0.95, r);
            float shimmer = angularNoise(angle, 20.0, iTime * 8.0) * treble * edgeFactor * 0.15;
            result.rgb += accent * shimmer;
        }

        // ── Edge spectrum bars ──────────────────────────────
        // Mini equalizer bars along each zone wall, filling the space
        // between the circular rings and the rectangular zone boundary.
        {
            float innerD = -d;  // distance from zone edge inward (>0 inside)
            float barDepth = min(rectSize.x, rectSize.y) * 0.12; // how far bars extend inward
            float edgeProximity = 1.0 - smoothstep(0.0, barDepth, innerD); // 1 at edge, 0 further in

            if (edgeProximity > 0.01) {
                // Which wall are we near? Use localUV to determine bar position along each edge
                // Bottom edge: localUV.y near 1, bars map to localUV.x
                // Top edge: localUV.y near 0, bars map to localUV.x
                // Left edge: localUV.x near 0, bars map to localUV.y
                // Right edge: localUV.x near 1, bars map to localUV.y

                float barU = 0.0;  // position along the wall (0-1)
                float wallWeight = 0.0;

                // Blend contributions from all nearby walls
                float bW = smoothstep(1.0 - barDepth / rectSize.y, 1.0, localUV.y); // bottom
                float tW = smoothstep(barDepth / rectSize.y, 0.0, localUV.y);        // top
                float lW = smoothstep(barDepth / rectSize.x, 0.0, localUV.x);        // left
                float rW = smoothstep(1.0 - barDepth / rectSize.x, 1.0, localUV.x); // right

                // Weighted bar position: horizontal walls use x, vertical use y
                barU = (bW * localUV.x + tW * localUV.x + lW * localUV.y + rW * localUV.y)
                     / max(bW + tW + lW + rW, 0.001);
                wallWeight = max(max(bW, tW), max(lW, rW));

                if (hasAudio && wallWeight > 0.01) {
                    float specVal = audioBarSmooth(barU) * reactivity;

                    // Bar fill: grows inward from the wall proportional to amplitude
                    float barFill = smoothstep(0.0, max(specVal, 0.001), edgeProximity);

                    // Color: map bar position across the spectrum
                    vec3 barColor = mix(primary, accent, barU);
                    if (barU < 0.25) barColor = mix(bassCol, barColor, barU * 4.0);

                    // Subtle segmentation for that EQ bar look
                    float segments = fract(edgeProximity * 8.0 / max(specVal, 0.1));
                    float segGap = smoothstep(0.0, 0.15, segments) * smoothstep(1.0, 0.85, segments);

                    float barBright = barFill * wallWeight * specVal * segGap * glowIntensity * 0.35;
                    result.rgb += barColor * barBright;

                    // Glow behind bars on strong hits
                    float barGlow = wallWeight * specVal * 0.08 * glowIntensity;
                    result.rgb += barColor * barGlow;
                } else if (!hasAudio && idleAnim > 0.01) {
                    // Idle: faint pulsing wave along edges
                    float wave = sin(barU * PI * 6.0 - iTime * 1.2) * 0.5 + 0.5;
                    float idleBars = wave * edgeProximity * wallWeight * 0.04 * idleAnim;
                    result.rgb += primary * idleBars;
                }
            }
        }

        // ── Idle animation (when no audio) ──────────────────
        if (!hasAudio && idleAnim > 0.01) {
            // Gentle concentric pulse rings
            float idleRing = sin((r - iTime * 0.2) * PI * 8.0) * 0.5 + 0.5;
            idleRing *= exp(-r * 1.5) * 0.06 * idleAnim;
            result.rgb += primary * idleRing;

            // Slow angular gradient sweep
            float sweep = sin(angle * 2.0 + iTime * 0.3) * 0.5 + 0.5;
            result.rgb += mix(primary, accent, sweep) * 0.02 * idleAnim;
        }

        result.a = mix(fillOpacity * 0.7, fillOpacity, vitality);

        // Inner edge glow — highlighted gets a stronger inward glow
        float innerGlow = exp(d / mix(25.0, 12.0, vitality)) * mix(0.06, 0.2, vitality) * glowIntensity * (1.0 + energy);
        result.rgb += primary * innerGlow;

        // ── Zone label: Resonance Lens ───────────────────────
        {
            vec2 labelUv = fragCoord / max(iResolution, vec2(0.001));
            vec2 texel = 1.0 / max(iResolution, vec2(1.0));
            vec4 labelSample = texture(uZoneLabels, labelUv);
            float labelAlpha = labelSample.a;
            float labelGlowSpread = customParams[2].x >= 0.0 ? customParams[2].x : 3.0;
            float labelBrightness = customParams[2].y >= 0.0 ? customParams[2].y : 2.0;
            float labelAudioMul = customParams[2].z >= 0.0 ? customParams[2].z : 1.0;

            // Gaussian halo for ring emission zone
            float halo = 0.0;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    float w = exp(-float(dx * dx + dy * dy) * 0.3);
                    halo += texture(uZoneLabels, labelUv + vec2(float(dx), float(dy)) * texel * labelGlowSpread).a * w;
                }
            }
            halo /= 16.5;

            if (halo > 0.003) {
                float haloEdge = halo * (1.0 - labelAlpha);

                // Mini concentric rings radiating from label edge
                float ringDist = haloEdge * 20.0;
                float rings = sin(ringDist * PI * 3.0 - iTime * 4.0) * 0.5 + 0.5;
                rings *= haloEdge;

                // Angular energy sweep
                float sweep = pow(max(sin(angle * 2.0 + iTime * 1.5), 0.0), 4.0);
                vec3 ringCol = mix(primary, accent, sweep);
                float ringBright = rings * (0.5 + energy * 0.8) * glowIntensity;
                result.rgb += ringCol * ringBright;

                result.a = max(result.a, haloEdge * 0.5);
            }

            // Core: resonant amplification with chromatic tint
            if (labelAlpha > 0.01) {
                vec3 boosted = result.rgb * (labelBrightness + energy * 1.5 + bass * labelAudioMul);
                vec3 tint = mix(primary, bassCol, 0.3 + bass * 0.3);
                boosted += tint * 0.2 * glowIntensity;
                result.rgb = mix(result.rgb, boosted, labelAlpha);
                result.a = max(result.a, labelAlpha);
            }
        }
    }

    // ── Border ──────────────────────────────────────────────

    float coreWidth = borderWidth * mix(0.5, 0.9, vitality);
    float core = softBorder(d, coreWidth);
    if (core > 0.0) {
        float angle = atan(p.x, -p.y) / TAU + 0.5;

        // Audio-reactive border: highlighted pulses hard, dormant is static
        float borderEnergy = 1.0 + energy * mix(0.2, 1.0, vitality) + idlePulse * 0.3;
        vec3 coreColor = primary * glowIntensity * borderEnergy;

        // Flowing highlights — highlighted: fast animated flow; dormant: barely moving
        float flowSpeed = mix(0.3, 2.0, vitality);
        float flowRange = mix(0.1, 0.4, vitality);
        float flow = angularNoise(angle, 10.0, -iTime * flowSpeed) * flowRange + (1.0 - flowRange * 0.5);
        coreColor *= flow;

        // Highlighted: pulsing border width via a breathing sine wave
        if (isHighlighted) {
            float breathe = 0.8 + 0.2 * sin(iTime * 3.0 + energy * 4.0);
            coreColor *= breathe;
            // Accent edge: secondary color traces along the border
            float accentTrace = angularNoise(angle, 6.0, iTime * 2.5);
            coreColor = mix(coreColor, accent * glowIntensity * borderEnergy, accentTrace * 0.4);
        }

        // White-hot center — brighter for highlighted
        coreColor = mix(coreColor, vec3(1.0), core * mix(0.25, 0.6, vitality));

        // Bass flash
        if (hasAudio && bass > 0.5) {
            float flash = (bass - 0.5) * 2.0 * vitality;
            coreColor = mix(coreColor, bassCol * 2.0, flash * core * 0.3);
        }

        result.rgb = max(result.rgb, coreColor * core);
        result.a = max(result.a, core);
    }

    // ── Outer glow ──────────────────────────────────────────

    float baseGlowR = mix(8.0, 20.0, vitality);
    float bassGlowR = mix(2.0, 6.0, vitality);
    float glowRadius = baseGlowR + bassGlowR * (hasAudio ? bass * reactivity : idlePulse) + 5.0 * energy;
    if (d > 0.0 && d < glowRadius) {
        float glow1 = expGlow(d, glowRadius * 0.2, glowIntensity * mix(0.12, 0.35, vitality));
        float glow2 = expGlow(d, glowRadius * 0.5, glowIntensity * mix(0.04, 0.12, vitality));

        vec3 glowColor = primary;
        // Highlighted: dual-color glow
        if (isHighlighted) {
            float glowAngle = atan(p.x, -p.y) / TAU + 0.5;
            glowColor = mix(primary, accent, angularNoise(glowAngle, 4.0, iTime * 0.8) * 0.6);
        }
        // Bass pulses tint the outer glow
        if (hasAudio && bass > 0.3) {
            glowColor = mix(glowColor, bassCol, (bass - 0.3) * 1.5 * vitality);
        }

        result.rgb += glowColor * (glow1 + glow2);
        result.a = max(result.a, (glow1 + glow2) * 0.5);
    }

    return result;
}

// ─── Main ───────────────────────────────────────────────────────

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    // Audio analysis (computed once for all zones)
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    float overall = getOverallSoft();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);

        color = blendOver(color, zoneColor);
    }

    // Labels are composited per-zone inside renderZone() — no separate pass needed
    fragColor = clampFragColor(color);
}
