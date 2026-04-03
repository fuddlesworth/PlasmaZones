// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Voxel Terrain — Image Pass (compositing, zones, borders, labels, DOF)
//
// Reads the full-screen 3D scene from iChannel0 (buffer pass) and the
// depth buffer from uDepthBuffer (binding 12). Composites per-zone with
// borders, labels, inner edge glow, outer glow, depth-of-field, and vignette.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>
#include <multipass.glsl>
#include <depth.glsl>

// ─── Per-zone rendering ─────────────────────────────────────────────

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor,
                vec4 params, bool isHighlighted,
                float bass, float mids, float treble, float overall, bool hasAudio)
{
    float borderRadius = max(params.x, 6.0);
    float borderWidth  = max(params.y, 2.5);

    float reactivity   = customParams[0].x >= 0.0 ? customParams[0].x : 1.5;
    float edgeGlow     = customParams[0].w >= 0.0 ? customParams[0].w : 1.5;
    float fillOpacity  = customParams[1].w >= 0.0 ? customParams[1].w : 0.92;
    float bassImpact   = customParams[2].z >= 0.0 ? customParams[2].z : 2.0;
    float idleSpeed    = customParams[2].w >= 0.0 ? customParams[2].w : 1.0;
    float dofStrength  = customParams[3].y >= 0.0 ? customParams[3].y : 0.5;

    vec3 primary   = colorWithFallback(customColors[0].rgb, vec3(0.06, 0.08, 0.18));
    vec3 accent    = colorWithFallback(customColors[1].rgb, vec3(0.0, 0.83, 1.0));
    vec3 bassCol   = colorWithFallback(customColors[2].rgb, vec3(0.9, 0.0, 0.67));
    vec3 wireColor = colorWithFallback(customColors[3].rgb, vec3(0.6, 0.7, 0.9));

    float energy = hasAudio ? overall * reactivity : 0.0;

    float vitality = zoneVitality(isHighlighted);
    if (!isHighlighted) {
        primary = vitalityDesaturate(primary, 0.3);
        accent = vitalityDesaturate(accent, 0.3);
        bassCol = vitalityDesaturate(bassCol, 0.3);
        wireColor *= 0.5;
        edgeGlow *= 0.4;
        reactivity *= 0.5;
    }

    vec2 rectPos  = zoneRectPos(rect);
    vec2 rectSize = zoneRectSize(rect);
    vec2 center   = rectPos + rectSize * 0.5;
    vec2 p        = fragCoord - center;
    float d       = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec4 result = vec4(0.0);

    if (d < 0.0) {
        // Sample the 3D scene from the buffer pass at screen coordinates.
        // channelUv handles Y-flip for the correct backend.
        vec2 sceneUv = channelUv(0, fragCoord);

        // ── Depth-of-field blur ─────────────────────────────
        // Sample a 3x3 grid around screen center and focus on the nearest
        // terrain hit. A single center-pixel sample jumps between terrain
        // (depth ~0.2) and sky (depth 1.0) as the camera flies through the
        // voxel world, causing DOF to cycle on/off. Using the minimum
        // non-miss depth from a wider region keeps focus stable on the
        // nearest visible geometry.
        vec4 scene;
        if (dofStrength > 0.001) {
            float focalDepth = 1.0;
            for (int fy = -1; fy <= 1; fy++) {
                for (int fx = -1; fx <= 1; fx++) {
                    vec2 sampleCoord = iResolution * 0.5 + vec2(float(fx), float(fy)) * iResolution * 0.08;
                    float sd = readDepth(channelUv(0, sampleCoord));
                    if (sd < focalDepth) focalDepth = sd;
                }
            }
            // If all samples miss sky, use mid-range focus
            focalDepth = min(focalDepth, 0.5);

            float pixelDepth = readDepth(sceneUv);
            float coc = abs(pixelDepth - focalDepth) * 2.0 * dofStrength;

            // 3x3 box blur weighted by circle of confusion
            vec4 blurred = vec4(0.0);
            float total = 0.0;
            vec2 texelSize = 1.0 / max(iChannelResolution[0], vec2(1.0));
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    vec2 offset = vec2(float(dx), float(dy)) * texelSize * coc * 8.0;
                    blurred += texture(iChannel0, sceneUv + offset);
                    total += 1.0;
                }
            }
            scene = blurred / total;
        } else {
            scene = texture(iChannel0, sceneUv);
        }

        // Apply vitality to sampled scene — non-highlighted zones are dimmer and
        // desaturated so you can immediately tell which zone is active.  The buffer
        // pass renders the 3D world once (shared), so without this modulation all
        // zones would show identical terrain.
        vec3 sceneRgb = scene.rgb;
        if (!isHighlighted) {
            // Desaturate toward luminance
            float lum = dot(sceneRgb, vec3(0.2126, 0.7152, 0.0722));
            sceneRgb = mix(vec3(lum), sceneRgb, 0.45);
            // Darken
            sceneRgb *= 0.4;
        }
        result.rgb = sceneRgb;
        result.a = fillOpacity;

        // Volumetric glow alpha contribution — reconstruct from depth buffer.
        // Buffer pass writes oDepth = hit.t / 60.0 (0=near, 1=far/miss).
        // Single-pass used: hit ? 0.5 * smoothstep(5.0, 40.0, hit.t) : 0.4
        // Reconstruct hit.t from depth, then apply the same formula.
        float pixDepth = readDepth(sceneUv);
        float hitT = pixDepth * 60.0;  // undo normalization
        float volG = (pixDepth >= 1.0)
            ? 0.4  // miss — same as single-pass
            : min(0.5 * smoothstep(5.0, 40.0, hitT), 3.0);
        float volMul;
        if (hasAudio) {
            volMul = 0.02 + energy * 0.06;
        } else {
            volMul = max(0.01 + sin(iTime * 0.6) * 0.005 * idleSpeed, 0.0);
        }
        result.a = max(result.a, min(volG * volMul * 0.2, fillOpacity));

        // Inner edge glow
        float innerGlow = exp(d / mix(30.0, 14.0, vitality)) * mix(0.03, 0.1, vitality);
        innerGlow *= edgeGlow * (0.4 + energy * 0.3);
        result.rgb += primary * innerGlow;

        // Zone labels — holographic voxel HUD style
        if (customParams[3].x > 0.5) {
            vec2 labelUv = fragCoord / max(iResolution, vec2(0.001));
            vec2 texel = 1.0 / max(iResolution, vec2(1.0));
            vec4 labelSample = texture(uZoneLabels, labelUv);
            float labelAlpha = labelSample.a;

            // Gaussian halo with wider spread
            float halo = 0.0;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    float w = exp(-float(dx * dx + dy * dy) * 0.3);
                    halo += texture(uZoneLabels, labelUv + vec2(float(dx), float(dy)) * texel * 3.5).a * w;
                }
            }
            halo /= 16.5;

            if (halo > 0.003) {
                float haloEdge = halo * (1.0 - labelAlpha);

                // Chromatic edge fringe — R and B channels offset
                float rH = texture(uZoneLabels, labelUv + vec2(texel.x * 2.5, 0.0)).a;
                float bH = texture(uZoneLabels, labelUv - vec2(texel.x * 2.5, 0.0)).a;
                vec3 chromaFringe = vec3(rH, 0.0, bH) * (1.0 - labelAlpha) * accent * 0.3;
                result.rgb += chromaFringe;

                // Palette-cycling halo (shifts accent→bassCol over time)
                float haloT = length(labelUv - 0.5) + iTime * 0.08;
                vec3 haloCol = mix(accent, bassCol, 0.5 + 0.5 * sin(haloT * 3.0));
                float haloBright = haloEdge * edgeGlow * 0.5;

                // Holographic scanlines through the halo
                float scan = 0.7 + 0.3 * sin(fragCoord.y * 0.8 + iTime * 4.0);
                haloBright *= scan;

                // Audio: bass pulses the halo, treble sparks
                if (hasAudio) {
                    haloBright *= 1.0 + bass * bassImpact * 0.4;
                    // Treble sparkle noise in the halo
                    float sparkNoise = hash21(floor(labelUv * 40.0) + vec2(floor(iTime * 6.0)));
                    float spark = smoothstep(0.8, 0.95, sparkNoise) * treble * 1.5;
                    result.rgb += mix(vec3(1.0), accent, 0.3) * haloEdge * spark;
                }

                result.rgb += haloCol * haloBright;
                result.a = max(result.a, haloEdge * 0.4);
            }

            if (labelAlpha > 0.01) {
                // Core: bright holographic text with accent color
                vec3 core = result.rgb * 2.0 + accent * 0.15;

                // Data-stream flicker (irregular, not regular pulse)
                float flicker = 0.9 + 0.1 * sin(iTime * 17.0) * sin(iTime * 23.0);
                core *= flicker;

                // Audio: bass pumps brightness, treble adds glitch bands
                if (hasAudio) {
                    core *= 1.0 + bass * 0.4;
                    float band = step(0.88, fract(fragCoord.y * 0.12 + iTime * 5.0));
                    core = mix(core, bassCol * 1.5, band * treble * 0.3);
                } else {
                    // Idle: gentle pulse
                    core *= 0.9 + 0.1 * sin(iTime * idleSpeed * 2.0);
                }

                result.rgb = mix(result.rgb, core, labelAlpha);
                result.a = max(result.a, labelAlpha);
            }
        }
    }

    // ── Border ────────────────────────────────────────────

    float coreWidth = borderWidth * mix(0.5, 0.9, vitality);
    float core = softBorder(d, coreWidth);
    if (core > 0.0) {
        float borderAngle = atan(p.x, -p.y) / TAU + 0.5;
        float borderEnergy = 1.0 + energy * mix(0.2, 0.8, vitality);
        vec3 coreColor = primary * edgeGlow * borderEnergy;

        float flowSpeed = mix(0.3, 1.5, vitality);
        float flow = angularNoise(borderAngle, 12.0, -iTime * flowSpeed);
        float segments = fract(borderAngle * 16.0 - iTime * flowSpeed);
        float segPulse = smoothstep(0.0, 0.1, segments) * smoothstep(1.0, 0.9, segments);
        coreColor *= mix(0.6, 1.2, flow) * mix(0.8, 1.0, segPulse);

        if (isHighlighted) {
            float breathe = 0.85 + 0.15 * sin(iTime * 2.5 + energy * 3.0);
            coreColor *= breathe;
            float sparkle = pow(max(sin(borderAngle * TAU * 8.0 + iTime * 3.0), 0.0), 8.0);
            coreColor = mix(coreColor, accent * edgeGlow * borderEnergy, sparkle * 0.5);
        }

        coreColor = mix(coreColor, wireColor, core * mix(0.2, 0.5, vitality));

        if (hasAudio && bass > 0.5) {
            float flash = (bass - 0.5) * 2.0 * vitality;
            coreColor = mix(coreColor, bassCol * 2.0, flash * core * 0.3);
        }

        result.rgb = max(result.rgb, coreColor * core);
        result.a = max(result.a, core);
    }

    // ── Outer glow ────────────────────────────────────────

    float baseGlowR = mix(6.0, 16.0, vitality);
    float glowRadius = baseGlowR + (hasAudio ? bass * reactivity * 5.0 : sin(iTime * 0.8) * 2.0);
    glowRadius += energy * 4.0;
    if (d > 0.0 && d < glowRadius) {
        float glow1 = expGlow(d, glowRadius * 0.2, edgeGlow * mix(0.08, 0.25, vitality));
        float glow2 = expGlow(d, glowRadius * 0.5, edgeGlow * mix(0.03, 0.08, vitality));

        vec3 glowColor = primary;
        if (isHighlighted) {
            float glowAngle = atan(p.x, -p.y) / TAU + 0.5;
            glowColor = mix(primary, accent, angularNoise(glowAngle, 5.0, iTime * 0.6) * 0.5);
        }
        if (hasAudio && bass > 0.3) {
            glowColor = mix(glowColor, bassCol, (bass - 0.3) * vitality);
        }

        result.rgb += glowColor * (glow1 + glow2);
        result.a = max(result.a, (glow1 + glow2) * 0.4);
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
    // Raw audio (not soft-dampened) — this is an audio visualizer,
    // we want the full dynamic range of the signal.
    float bass    = getBass();
    float mids    = getMids();
    float treble  = getTreble();
    float overall = getOverall();

    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;

        vec4 zoneColor = renderZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], zoneParams[i].z > 0.5,
            bass, mids, treble, overall, hasAudio);

        color = blendOver(color, zoneColor);
    }

    // Vignette
    vec2 q = fragCoord / max(iResolution, vec2(1.0));
    float vig = 0.5 + 0.5 * pow(16.0 * q.x * q.y * (1.0 - q.x) * (1.0 - q.y), 0.1);
    color.rgb *= vig;

    fragColor = clampFragColor(color);
}
