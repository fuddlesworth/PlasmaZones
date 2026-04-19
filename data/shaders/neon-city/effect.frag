// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Neon City — Image Pass (compositing, zones, borders, labels, DOF)
//
// Reads the full-screen 3D cityscape from iChannel0 (buffer pass) and the
// depth buffer from uDepthBuffer. Composites per-zone with rounded borders,
// holographic labels, inner edge glow, outer glow, DOF, and a vignette.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>
#include <multipass.glsl>
#include <depth.glsl>

// ─── Per-zone rendering ─────────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 params, bool isHighlighted,
                float bass, float mids, float treble, float overall, bool hasAudio)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    float reactivity   = customParams[0].x >= 0.0 ? customParams[0].x : 1.5;
    float bassImpact   = customParams[2].x >= 0.0 ? customParams[2].x : 1.5;
    float trebleImpact = customParams[2].y >= 0.0 ? customParams[2].y : 1.5;
    float midsImpact   = customParams[2].z >= 0.0 ? customParams[2].z : 1.0;
    float idleSpeed    = customParams[2].w >= 0.0 ? customParams[2].w : 1.0;
    float fillOpacity  = customParams[3].x >= 0.0 ? customParams[3].x : 0.92;
    float showLabels   = customParams[3].y;  // bool: >0.5 = show
    float dofStrength  = customParams[3].z >= 0.0 ? customParams[3].z : 0.35;
    float edgeGlow     = customParams[3].w >= 0.0 ? customParams[3].w : 1.4;

    vec3 accent  = colorWithFallback(customColors[1].rgb, vec3(0.50, 1.50, 2.00));
    vec3 bassCol = colorWithFallback(customColors[2].rgb, vec3(0.00, 0.00, 1.50));
    vec3 lightC  = colorWithFallback(customColors[3].rgb, vec3(0.80, 0.45, 0.18));

    // Audio channels scaled by reactivity AND per-band impact — consistent
    // with the buffer pass so the reactivity slider affects every element.
    float aBass   = hasAudio ? bass   * reactivity * bassImpact   : 0.0;
    float aMids   = hasAudio ? mids   * reactivity * midsImpact   : 0.0;
    float aTreble = hasAudio ? treble * reactivity * trebleImpact : 0.0;
    float energy  = hasAudio ? overall * reactivity : 0.0;

    float vitality = zoneVitality(isHighlighted);

    if (!isHighlighted) {
        accent  = vitalityDesaturate(accent, 0.3);
        bassCol = vitalityDesaturate(bassCol, 0.3);
        lightC  = vitalityDesaturate(lightC, 0.3);
        edgeGlow *= 0.4;
        aBass    *= 0.5;
        aMids    *= 0.5;
        aTreble  *= 0.5;
        energy   *= 0.5;
    }

    float px = pxScale();

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        vec2 sceneUv = channelUv(0, fragCoord);

        // ── Depth-of-field blur ─────────────────────────────
        // Find nearest visible depth across a wide 3×3 lookup near screen
        // center so focal depth doesn't flicker when the camera crosses a
        // thin building and a distant one alternates.
        vec4 scene;
        if (dofStrength > 0.001) {
            float focalDepth = 1.0;
            for (int fy = -1; fy <= 1; fy++) {
                for (int fx = -1; fx <= 1; fx++) {
                    vec2 sampleCoord = iResolution * 0.5
                        + vec2(float(fx), float(fy)) * iResolution * 0.08;
                    float sd = readDepth(channelUv(0, sampleCoord));
                    if (sd < focalDepth) focalDepth = sd;
                }
            }
            focalDepth = min(focalDepth, 0.6);

            float pixelDepth = readDepth(sceneUv);
            float coc = abs(pixelDepth - focalDepth) * 2.0 * dofStrength;

            vec4 blurred = vec4(0.0);
            float total = 0.0;
            vec2 texelSize = 1.0 / max(iChannelResolution[0], vec2(1.0));
            for (int by = -1; by <= 1; by++) {
                for (int bx = -1; bx <= 1; bx++) {
                    vec2 offset = vec2(float(bx), float(by)) * texelSize * coc * 8.0;
                    blurred += texture(iChannel0, sceneUv + offset);
                    total += 1.0;
                }
            }
            scene = blurred / total;
        } else {
            scene = texture(iChannel0, sceneUv);
        }

        // Non-highlighted zones: desaturate + darken sampled scene so the
        // active zone clearly reads as the focus.
        vec3 sceneRgb = scene.rgb;
        if (!isHighlighted) {
            float lum = dot(sceneRgb, vec3(0.2126, 0.7152, 0.0722));
            sceneRgb = mix(vec3(lum), sceneRgb, 0.45);
            sceneRgb *= 0.4;
        }
        result.rgb = sceneRgb;
        result.a   = fillOpacity;

        // Inner edge glow — SDF-based tint that rides the zone's rim.
        // Fade distance scales with resolution via pxScale().
        float innerGlow = exp(d / (mix(28.0, 14.0, vitality) * px))
                        * mix(0.04, 0.12, vitality);
        innerGlow *= edgeGlow * (0.5 + energy * 0.4 + aMids * 0.25);
        vec3 innerCol = mix(accent, bassCol, clamp(energy, 0.0, 1.0));
        result.rgb += innerCol * innerGlow;

        // ── Zone labels: white core + neon-sign halo ─────────
        // Structure follows the generic "soft Gaussian halo + bright core"
        // that reads cleanly, but swaps the generic vocabulary
        // (chromatic fringe, CRT scanlines, data-stream sin*sin flicker)
        // for the city's own sign behavior: per-zone flash phase derived
        // from the zone's rect, sign-palette halo (bassCol → signColorB
        // white-hot on flash), occasional treble-driven dropout, bass
        // pump. The label itself stays white — it's the halo/flash that
        // carries the city's identity.
        if (showLabels > 0.5) {
            vec2 labelUv = fragCoord / max(iResolution, vec2(0.001));
            vec2 texel   = 1.0 / max(iResolution, vec2(1.0));
            float labelAlpha = texture(uZoneLabels, labelUv).a;

            // Per-zone seed derived from the zone's normalized rect coords
            // scaled by a prime to spread bits — one seed per zone so the
            // entire label animates in sync (a per-fragment seed tiles the
            // label into visibly colored rectangles).
            float signSeed = hash21(rect.xy * 997.0 + rect.zw * 131.0);
            float phase    = signSeed * TAU;

            // Sign flash rate: 2.5-5Hz per-zone. When no audio, idleSpeed
            // multiplies the rate so users can slow down/speed up idle
            // animation to taste.
            float idleScale = hasAudio ? 1.0 : idleSpeed;
            float flashRate = (2.5 + signSeed * 2.5) * idleScale;
            float raw       = 0.5 + 0.5 * cos(flashRate * iTime + phase);
            float flash     = 0.25 + 0.75 * smoothstep(0.1, 0.9, raw);

            // Treble-driven neon glitch — noticeable blackouts per zone.
            float glitch = 1.0;
            if (aTreble > 0.09) {
                float slot = floor(iTime * (6.0 + aTreble * 10.0));
                float gh = hash21(vec2(signSeed * 97.0, slot));
                glitch = 1.0 - step(0.85 - aTreble * 0.18, gh) * 0.9;
            }

            // Gaussian halo — pixel radius scaled by pxScale() so angular
            // reach is resolution-independent.
            vec2 haloReach = texel * 3.5 * px;
            float halo = 0.0;
            for (int hy = -2; hy <= 2; hy++) {
                for (int hx = -2; hx <= 2; hx++) {
                    float w = exp(-float(hx * hx + hy * hy) * 0.3);
                    halo += texture(uZoneLabels,
                        labelUv + vec2(float(hx), float(hy)) * haloReach).a * w;
                }
            }
            halo /= 16.5;

            // Halo stays in the sign palette (bassCol → accent on flash),
            // never drifts to white. Bass pump further saturates.
            vec3 haloColor = mix(bassCol * 1.6, accent * 1.6, flash)
                           * (1.0 + aBass * 0.8);

            if (halo > 0.003) {
                float haloEdge = halo * (1.0 - labelAlpha);
                float haloPower = edgeGlow * (0.3 + flash * 1.1) * glitch;
                result.rgb += haloColor * haloEdge * haloPower;
                result.a = max(result.a, haloEdge * 0.45);

                // Treble sparkle: additive hot-white bursts on the halo
                // edge — the label's "character flash" analogue.
                if (aTreble > 0.06) {
                    float sslot = floor(iTime * (5.0 + aTreble * 9.0));
                    float shash = hash21(vec2(signSeed * 31.0, sslot));
                    float spark = step(0.7, shash) * aTreble * 0.8;
                    result.rgb += vec3(2.0) * haloEdge * spark * glitch;
                }
            }

            if (labelAlpha > 0.01) {
                // Per-zone hue: each zone's label picks a tube color from
                // the sign palette (blue ↔ cyan) using signSeed.
                vec3 baseColor = mix(bassCol, accent, signSeed);
                vec3 core = baseColor * 1.7;

                // Arc-flash at the peak of the flash envelope only.
                core = mix(core, vec3(2.7),
                           smoothstep(0.75, 1.0, flash));

                // Bass kick briefly pushes the tube white-hot.
                float bassPunch = clamp(aBass - 0.35, 0.0, 0.55);
                core = mix(core, vec3(2.5), bassPunch);

                core *= glitch * (1.0 + aTreble * 0.25);
                result.rgb = mix(result.rgb, core, labelAlpha);
                result.a = max(result.a, labelAlpha);
            }
        }
    }

    // ── Border ───────────────────────────────────────────────
    // timeSin/timeCos used wherever phase continuity matters across the
    // K_TIME_WRAP boundary (~17 min).  Raw sin(iTime*k) would visibly
    // jump.  angularNoise/segments are noise/sawtooth and tolerate a
    // small reseed at the wrap, so they keep using iTime directly.
    float coreWidth = borderWidth * mix(0.5, 0.9, vitality);
    float borderCore = softBorder(d, coreWidth);
    if (borderCore > 0.0) {
        float borderAngle   = atan(p.x, -p.y) / TAU + 0.5;
        float borderEnergy  = 1.0 + energy * mix(0.2, 0.8, vitality)
                            + aMids * 0.15;

        // Slow 0.5Hz color sweep — must use timeSin() (wrap would snap).
        // When no audio, idleSpeed scales the sweep rate.
        float sweepRate = 0.5 * (hasAudio ? 1.0 : idleSpeed);
        vec3 coreColor = mix(accent, bassCol, 0.5 + 0.5 * timeSin(sweepRate))
                        * edgeGlow * borderEnergy;

        float flowRate = mix(0.3, 1.5, vitality) * (hasAudio ? 1.0 : idleSpeed);
        float flow     = angularNoise(borderAngle, 12.0, -iTime * flowRate);
        float segments = fract(borderAngle * 16.0 - iTime * flowRate);
        float segPulse = smoothstep(0.0, 0.1, segments) * smoothstep(1.0, 0.9, segments);
        coreColor *= mix(0.6, 1.2, flow) * mix(0.8, 1.0, segPulse);

        if (isHighlighted) {
            float breathe = 0.85 + 0.15 * timeSin(2.5, energy * 3.0);
            coreColor *= breathe;
            // Per-pixel sparkle: phase = borderAngle*TAU*8 + timeCos(3.0)
            // is harder to factor; use timeSin with the angle as offset.
            float sparkle = pow(max(timeSin(3.0, borderAngle * TAU * 8.0), 0.0), 8.0);
            coreColor = mix(coreColor, lightC * edgeGlow * borderEnergy, sparkle * 0.5);
        }

        if (aBass > 0.5) {
            float bassFlash = (aBass - 0.5) * 2.0 * vitality;
            coreColor = mix(coreColor, lightC * 2.0, bassFlash * borderCore * 0.4);
        }

        result.rgb = max(result.rgb, coreColor * borderCore);
        result.a   = max(result.a, borderCore);
    }

    // ── Outer glow ───────────────────────────────────────────
    // baseGlowR scales with pxScale() for resolution-independent angular
    // reach (16px at 1080p == 32px at 4K).
    float baseGlowR  = mix(6.0, 16.0, vitality) * px;
    float pulseRate  = 0.8 * (hasAudio ? 1.0 : idleSpeed);
    float glowRadius = baseGlowR
        + (hasAudio ? aBass * 5.0 : timeSin(pulseRate) * 2.0 * px);
    glowRadius += energy * 4.0 * px;

    if (d > 0.0 && d < glowRadius) {
        float glow1 = expGlow(d, glowRadius * 0.2, edgeGlow * mix(0.08, 0.25, vitality));
        float glow2 = expGlow(d, glowRadius * 0.5, edgeGlow * mix(0.03, 0.08, vitality));

        vec3 glowColor = mix(accent, bassCol, clamp(energy * 0.8, 0.0, 1.0));
        if (isHighlighted) {
            float glowAngle = atan(p.x, -p.y) / TAU + 0.5;
            glowColor = mix(glowColor, lightC, angularNoise(glowAngle, 5.0, iTime * 0.6) * 0.5);
        }
        if (aBass > 0.3) {
            glowColor = mix(glowColor, lightC, (aBass - 0.3) * vitality);
        }

        result.rgb += glowColor * (glow1 + glow2);
        result.a   = max(result.a, (glow1 + glow2) * 0.4);
    }

    return result;
}

// ─── Main ───────────────────────────────────────────────────────────

void main() {
    vec2 fragCoord = vFragCoord;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBass();
    float mids    = getMids();
    float treble  = getTreble();
    float overall = getOverall();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderZone(fragCoord, rect, zoneParams[i],
            zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);

        color = blendOver(color, zoneColor);
    }

    // Vignette
    vec2 q = fragCoord / max(iResolution, vec2(1.0));
    float vig = 0.5 + 0.5 * pow(16.0 * q.x * q.y * (1.0 - q.x) * (1.0 - q.y), 0.1);
    color.rgb *= vig;

    fragColor = clampFragColor(color);
}
