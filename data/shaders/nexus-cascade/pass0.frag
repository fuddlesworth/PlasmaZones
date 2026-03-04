// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Nexus Cascade — Buffer Pass 0: Animated plasma / flow base
// Fullscreen output to iChannel0. No channel inputs.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>


// Rotate uv around center by angle (radians)
vec2 rotate2d(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

float plasma(vec2 uv, float t) {
    float v = 0.0;
    float px = uv.x + sin(t * 0.7) * 0.3;
    float py = uv.y + cos(t * 0.5) * 0.2;
    v += sin((px + t * 0.8) * 8.0);
    v += sin((py - t * 0.6) * 7.0);
    v += sin((uv.x + uv.y + t * 1.2 + uv.x * 2.0) * 12.0);
    v += sin(length(uv - 0.5) * 15.0 - t * 2.0);
    v += sin(atan(uv.y - 0.5, uv.x - 0.5) * 4.0 + t * 1.5) * 0.4;
    return v * 0.25 + 0.5;
}

float flowNoise(vec2 p, float t) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    float n = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
    float angle = atan(p.y, p.x);
    float r = length(p);
    float swirl = sin(angle * 3.0 + t * 2.0) * 0.5 + sin(r * 2.0 - t * 1.2) * 0.5;
    return n + 0.15 * swirl;
}

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 uv = fragCoord / max(iResolution.xy, vec2(1.0));
    vec2 centered = uv - 0.5;

    float speed = customParams[0].x >= 0.0 ? customParams[0].x : 0.4;
    float scale = customParams[0].y >= 0.0 ? customParams[0].y : 4.0;
    float audioReact = customParams[0].z >= 0.0 ? customParams[0].z : 1.0;
    float eruptStr = customParams[0].w >= 0.0 ? customParams[0].w : 2.5;
    float swirlSens = customParams[1].w >= 0.0 ? customParams[1].w : 4.0;

    // Audio analysis
    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass    = getBassSoft();
    float mids    = getMidsSoft();
    float treble  = getTrebleSoft();
    // Base time for plasma animation (no bass speed hack)
    float t = iTime * speed;

    // ── Mids = Cascade Branching Swirl ──────────────────────────────────
    // Instead of uniformly accelerating swirl, mids create branching distortion
    // that varies spatially — like network paths splitting and merging. The swirl
    // angle is modulated by a position-dependent wave so different regions of the
    // plasma rotate at different rates, producing visible branching flow patterns.
    float baseSwirlA = t * 0.4;
    float baseSwirlB = t * -0.25;
    float branchWave = hasAudio
        ? sin(centered.x * 8.0 + iTime * 1.5) * cos(centered.y * 6.0 - iTime * 1.2)
          * mids * swirlSens * audioReact * 0.3
        : 0.0;
    float swirlA = baseSwirlA + branchWave;
    float swirlB = baseSwirlB - branchWave * 0.6;
    vec2 uvPlasma = rotate2d(centered, swirlA) + 0.5;
    vec2 uvFlow = rotate2d(centered, swirlB) + 0.5;

    float p = plasma(uvPlasma * scale, t);
    float f = flowNoise(uvFlow * scale * 2.0, t);

    vec3 c1 = colorWithFallback(customColors[0].rgb, vec3(0.35, 0.55, 0.95));
    vec3 c2 = colorWithFallback(customColors[1].rgb, vec3(0.9, 0.4, 0.85));
    vec3 c3 = colorWithFallback(customColors[2].rgb, vec3(0.2, 0.8, 0.75));

    vec3 col = mix(c1, c2, p);
    col = mix(col, c3, f * 0.5);
    float mod = 0.65 + 0.35 * (p * f + sin(t + uvPlasma.x * TAU + uvFlow.y * 4.0) * 0.15);
    col *= mod;

    // ── Bass = Plasma ERUPTION Bursts ─────────────────────────────────
    // Localized solar-flare-like hot spots that bloom outward on bass hits.
    // Three eruption points that slowly drift; each bursts additively over plasma.
    if (hasAudio && bass > 0.05) {
        for (int ei = 0; ei < 3; ei++) {
            // Eruption center drifts slowly over time
            float drift = float(ei) * 2.094 + iTime * 0.15; // 2pi/3 spacing + slow drift
            vec2 eruptCenter = vec2(
                0.5 + 0.25 * sin(drift * 0.7 + float(ei) * 1.3),
                0.5 + 0.25 * cos(drift * 0.9 + float(ei) * 0.7)
            );

            // Cycling eruption radius: blooms outward then resets
            float cycle = fract(iTime * (0.8 + float(ei) * 0.3));
            float eruptRadius = cycle * 0.35; // expands up to 0.35 UV units

            // Distance from this pixel to the eruption center
            float eDist = length(uv - eruptCenter);

            // Soft ring-shaped eruption front, brighter at the expanding edge
            float ringWidth = 0.06 + bass * 0.04;
            float ring = smoothstep(eruptRadius + ringWidth, eruptRadius, eDist)
                       * smoothstep(eruptRadius - ringWidth * 2.0, eruptRadius, eDist);

            // Core glow (solid bright center that fades as it expands)
            float core = smoothstep(eruptRadius * 0.8, 0.0, eDist) * (1.0 - cycle * 0.7);

            // Combined eruption brightness, scaled by bass intensity
            float eruption = (ring * 0.7 + core) * bass * eruptStr * audioReact;

            // Hot eruption color: shift existing plasma color toward white-hot
            vec3 hotColor = mix(col, vec3(1.0, 0.95, 0.85), 0.6);
            col += hotColor * eruption;
        }
    }

    // ── Treble = Network Grid Overlay ──────────────────────────────
    // Treble reveals a subtle grid pattern in the plasma, like the underlying
    // network topology becoming visible at high frequencies. The grid lines
    // pulse at treble-driven intervals and shift position with time.
    if (hasAudio && treble > 0.08) {
        float gridScale = 12.0;
        vec2 gridUV = uv * gridScale + iTime * 0.2;
        float gridX = smoothstep(0.45, 0.5, abs(fract(gridUV.x) - 0.5));
        float gridY = smoothstep(0.45, 0.5, abs(fract(gridUV.y) - 0.5));
        float grid = max(gridX, gridY);
        // Grid brightness pulses with treble and varies along the grid
        float gridPulse = 0.5 + 0.5 * sin(iTime * 4.0 + gridUV.x * 2.0 + gridUV.y * 2.0);
        col += col * grid * gridPulse * treble * 0.5;
    }

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
