// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

/*
 * SONIC RIPPLE — Concentric Audio Rings
 *
 * The audio spectrum is mapped onto concentric rings radiating from each
 * zone's center.  Low frequencies sit at the core, high frequencies at the
 * edge.  Each ring's brightness and thickness IS the amplitude of its
 * frequency band, so every beat, every note is immediately visible.
 *
 * Bass kicks launch expanding shockwave rings that travel outward and fade.
 * The entire pattern slowly rotates and breathes with overall energy.
 *
 * Parameters (customParams):
 *   [0].x = reactivity       — audio sensitivity multiplier
 *   [0].y = ringCount        — how many spectrum rings to draw
 *   [0].z = bassShockwave    — strength of bass-triggered shockwaves
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

// ─── Noise ──────────────────────────────────────────────────────

float noise1D(float x) {
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(hash11(i), hash11(i + 1.0), f);
}

float noise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Seamless angular noise: sample 2D noise on a circle to avoid atan() seam
float angularNoise(float angle, float freq, float seed) {
    vec2 circlePos = vec2(cos(angle), sin(angle)) * freq;
    return noise2D(circlePos + seed);
}

// ─── Frequency analysis ─────────────────────────────────────────

float getBass() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    int n = min(iAudioSpectrumSize, 8);
    for (int i = 0; i < n; i++) sum += audioBar(i);
    return sum / float(n);
}

float getMids() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    int lo = iAudioSpectrumSize / 4;
    int hi = iAudioSpectrumSize * 3 / 4;
    for (int i = lo; i < hi && i < iAudioSpectrumSize; i++) sum += audioBar(i);
    return sum / float(max(hi - lo, 1));
}

float getTreble() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    int lo = iAudioSpectrumSize * 3 / 4;
    for (int i = lo; i < iAudioSpectrumSize; i++) sum += audioBar(i);
    return sum / float(max(iAudioSpectrumSize - lo, 1));
}

float getOverall() {
    if (iAudioSpectrumSize <= 0) return 0.0;
    float sum = 0.0;
    for (int i = 0; i < iAudioSpectrumSize; i++) sum += audioBar(i);
    return sum / float(iAudioSpectrumSize);
}

// ─── Shockwave ring ─────────────────────────────────────────────

// Returns intensity of a single expanding shockwave ring at distance r
// birth: time the wave was born, speed: expansion rate
float shockwave(float r, float birth, float speed) {
    float age = iTime - birth;
    if (age < 0.0 || age > 2.0) return 0.0;

    float radius = age * speed;
    float width = 0.03 + age * 0.02;  // ring gets wider as it expands
    float ring = exp(-pow((r - radius) / width, 2.0));
    float fade = 1.0 - age * 0.5;     // fade over 2 seconds
    return ring * max(fade, 0.0);
}

// ─── Per-zone rendering ─────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    // Parameters with defaults
    float reactivity    = customParams[0].x >= 0.0 ? customParams[0].x : 1.5;
    float ringCount     = customParams[0].y >= 0.0 ? customParams[0].y : 24.0;
    float bassShock     = customParams[0].z >= 0.0 ? customParams[0].z : 1.5;
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
        bassShock *= 1.5;       // bigger shockwaves
    } else {
        // Dormant: desaturated, dimmer, slower
        primary = mix(primary, vec3(dot(primary, vec3(0.299, 0.587, 0.114))), 0.5); // desaturate
        accent = mix(accent, vec3(dot(accent, vec3(0.299, 0.587, 0.114))), 0.5);
        bassCol = mix(bassCol, vec3(dot(bassCol, vec3(0.299, 0.587, 0.114))), 0.5);
        glowIntensity *= 0.5;
        reactivity *= 0.6;
        rotSpeed *= 0.5;        // barely moving
        bassShock *= 0.3;       // muted shockwaves
        ringCount = max(ringCount * 0.5, 8.0); // fewer rings
    }

    // Zone geometry
    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    vec2 localUV  = zoneLocalUV(fragCoord, rectPos, rectSize);
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    // Audio analysis
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass     = getBass();
    float mids     = getMids();
    float treble   = getTreble();
    float overall  = getOverall();

    float energy    = hasAudio ? overall * reactivity : 0.0;
    float idlePulse = hasAudio ? 0.0 : (0.5 + 0.5 * sin(iTime * 0.8 * PI)) * idleAnim;

    vec4 result = vec4(0.0);

    // ── Zone interior ───────────────────────────────────────

    if (d < 0.0) {
        // Normalized radial distance from zone center (0 = center, 1 = edge)
        // Use the shorter axis as reference so rings are circular, not elliptical
        float refSize = min(rectSize.x, rectSize.y) * 0.5;
        float r = length(p) / refSize;

        // Slow rotation of the coordinate space
        float angle = atan(p.y, p.x) + iTime * rotSpeed;

        // ── Concentric spectrum rings ───────────────────────
        // Map frequency bands to radial distance: bass at center, treble at edge
        float ringBright = 0.0;
        vec3 ringColor = vec3(0.0);

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
                float thickness = mix(0.004, 0.008, vitality) + specVal * mix(0.012, 0.03, vitality);

                // Angular wobble: the ring isn't a perfect circle (seamless around the circle)
                float wobble = angularNoise(angle, 3.0, t * 20.0 + iTime * 0.5) * specVal * 0.03;
                float ringDist = abs(r - ringR - wobble);

                // Ring intensity: gaussian falloff from center of ring
                float ring = exp(-pow(ringDist / thickness, 2.0)) * specVal;

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

        // ── Bass shockwaves ─────────────────────────────────
        // Multiple overlapping shockwave rings triggered by bass beats
        float shockTotal = 0.0;
        if (hasAudio && bassShock > 0.01) {
            // Create shockwaves at regular intervals modulated by bass
            // Each slot fires when bass crosses a threshold
            for (int s = 0; s < 4; s++) {
                float slot = float(s);
                // Stagger birth times: each slot fires at a different phase
                float period = 0.5 + slot * 0.15;
                float birth = floor(iTime / period) * period;
                float shock = shockwave(r, birth, 0.4 + slot * 0.1) * bass * bassShock;
                shockTotal += shock;
            }
        }

        // ── Audio-reactive nebula background ────────────────
        // Warped noise field fills the space between and behind rings.
        // Audio drives the warp intensity, color, and flow speed.

        // Warp the UV with audio-driven distortion
        vec2 nebUV = localUV * 2.5;
        float warpAmt = 0.4 + energy * 0.8 + idlePulse * 0.3;
        float n1 = noise2D(nebUV + iTime * 0.15);
        float n2 = noise2D(nebUV * 1.7 - iTime * 0.1 + 30.0);
        vec2 warp = vec2(n1, n2) * warpAmt;
        nebUV += warp;

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

        // Add shockwaves in bass color
        vec3 shockColor = mix(bassCol, vec3(1.0), 0.3);
        result.rgb += shockColor * shockTotal * glowIntensity * 0.5;

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
                    float barFill = smoothstep(0.0, specVal, edgeProximity);

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

        // ── Zone label integration ──────────────────────────
        // The number becomes a bright window into an amplified version
        // of the zone content underneath, with a matching glow bleed.
        {
            vec2 labelUv = fragCoord / max(iResolution, vec2(0.001));
            vec2 texel = 1.0 / max(iResolution, vec2(1.0));

            // Sample a wider area for the glow halo (spread label alpha outward)
            float halo = 0.0;
            float labelAlpha = texture(uZoneLabels, labelUv).a;
            for (int dy = -3; dy <= 3; dy++) {
                for (int dx = -3; dx <= 3; dx++) {
                    vec2 off = vec2(float(dx), float(dy)) * texel * 2.5;
                    halo += texture(uZoneLabels, labelUv + off).a;
                }
            }
            halo /= 49.0;

            // Glow halo: tinted bleed around the number, pulsing with energy
            if (halo > 0.005) {
                float haloEdge = halo * (1.0 - labelAlpha); // only the bleed, not the glyph itself
                vec3 haloColor = mix(primary, accent, 0.5 + 0.5 * sin(iTime * 0.7));
                float haloBright = haloEdge * (0.4 + energy * 0.8 + bass * 0.6) * glowIntensity;
                result.rgb += haloColor * haloBright;
            }

            // The number itself: amplify the zone content that's already been rendered
            if (labelAlpha > 0.01) {
                // Boost what's underneath — the rings/nebula become vivid inside the number
                vec3 boosted = result.rgb * (2.5 + energy * 2.0 + bass * 1.5);

                // Add a bright tinted core so the number is always readable
                vec3 labelTint = mix(primary, accent, 0.5 + 0.5 * sin(iTime * 0.5));
                vec3 hotCore = labelTint * (0.6 + energy * 0.4) * glowIntensity;
                boosted += hotCore;

                // Blend into result using label alpha
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
    float bassGlowR = mix(10.0, 35.0, vitality);
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

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5);

        color = blendOver(color, zoneColor);
    }

    // Labels are composited per-zone inside renderZone() — no separate pass needed
    fragColor = clampFragColor(color);
}
