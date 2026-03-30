// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Pulse Flow — Buffer Pass 0 (ping-pong feedback)
//
// Lightweight energy field with full audio reactivity. Single-octave curl noise
// advection, zone-edge emission, and feedback contraction. Bass drives emission
// intensity + radial pulses, mids shift the feedback spiral, treble injects sparks.
//
// ~8 noise2D per fragment (vs ~20+ in ember-trace/neon-phantom).
//
// Channel layout:
//   R = energy intensity (0-1)
//   G = freshness (1 = just emitted, decays for color aging)
//   B = flow direction encoded (for subtle motion coloring)
//   A = 1

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <audio.glsl>

// ─── Parameters ─────────────────────────────────────────────────────────────

float getIntensity()      { return customParams[0].x >= 0.0 ? customParams[0].x : 0.9; }
float getPersistence()    { return customParams[0].y >= 0.0 ? customParams[0].y : 0.955; }
float getEdgeWidth()      { return customParams[0].z >= 0.0 ? customParams[0].z : 14.0; }
float getFeedbackStr()    { return customParams[0].w >= 0.0 ? customParams[0].w : 0.5; }
float getDriftSpeed()     { return customParams[1].x >= 0.0 ? customParams[1].x : 0.35; }
float getRotationSpeed()  { return customParams[1].y >= 0.0 ? customParams[1].y : 0.03; }
float getAudioReact()     { return customParams[1].z >= 0.0 ? customParams[1].z : 1.0; }

// ─── Helpers ────────────────────────────────────────────────────────────────

vec2 rotate2d(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

// Single-octave curl noise — 4 noise2D calls (vs 12-16 in multi-octave)
vec2 curlNoise(vec2 p, float t) {
    float eps = 0.5;
    float n  = noise2D(p + vec2(0.0, eps) + t);
    float ns = noise2D(p - vec2(0.0, eps) + t);
    float ne = noise2D(p + vec2(eps, 0.0) + t);
    float nw = noise2D(p - vec2(eps, 0.0) + t);
    return vec2(n - ns, -(ne - nw)) / (2.0 * eps);
}

// Minimum distance to any zone border (borderRadius scaled by pxScale for
// resolution-independent corner shapes matching the effect pass).
float zoneEdgeSDF(vec2 fragCoord, float pxs) {
    float minDist = 1e6;
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec2 pos = zoneRectPos(zoneRects[i]);
        vec2 sz  = zoneRectSize(zoneRects[i]);
        float radius = max(zoneParams[i].x, 8.0) * pxs;
        vec2 center = pos + sz * 0.5;
        float d = sdRoundedBox(fragCoord - center, sz * 0.5, radius);
        minDist = min(minDist, abs(d));
        if (minDist < 1.0) break;
    }
    return minDist;
}

// Zone center in UV space (for bass radial pulses)
vec2 zoneCenter(int i, vec2 res) {
    vec2 pos = zoneRectPos(zoneRects[i]);
    vec2 sz  = zoneRectSize(zoneRects[i]);
    return (pos + sz * 0.5) / res;
}

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 res = max(iResolution.xy, vec2(1.0));
    vec2 uv = fragCoord / res;
    float aspect = res.x / res.y;
    float pxs = pxScale();

    // ── First frame: seed initial state ─────────────────────────────────────
    if (iFrame == 0) {
        float energy = 0.0;
        float edgeDist = zoneEdgeSDF(fragCoord, pxs);
        energy += smoothstep(40.0 * pxs, 0.0, edgeDist) * 0.3;
        energy += noise2D(uv * 8.0) * 0.05;
        fragColor = vec4(clamp(energy, 0.0, 1.0), energy, 0.0, 1.0);
        return;
    }

    // ── Parameters ──────────────────────────────────────────────────────────
    float intensity   = getIntensity();
    float persistence = getPersistence();
    float edgeW       = getEdgeWidth() * pxs;
    float feedbackStr = getFeedbackStr();
    float driftSpd    = getDriftSpeed();
    float rotSpd      = getRotationSpeed();
    float audioR      = getAudioReact();

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass     = getBassSoft();
    float mids     = getMidsSoft();
    float treble   = getTrebleSoft();

    float t = iTime;

    // ── 1. FEEDBACK TRANSFORM ───────────────────────────────────────────────
    // Gentle contraction + rotation creates spiraling inward flow.
    // Mids shift the spiral phase — changes feel with mid-range music.

    vec2 ctr = vec2(0.5);
    vec2 fromCenter = uv - ctr;
    fromCenter.x *= aspect;

    float feedbackAngle = t * rotSpd;
    // Mids: shift feedback rotation for organic topology changes
    if (hasAudio && mids > 0.03) {
        feedbackAngle += mids * audioR * 0.12;
    }

    // Feedback strength: 0 = no contraction (static), 1 = strong inward pull
    float contraction = 0.999 - clamp(feedbackStr, 0.0, 1.0) * 0.003;

    vec2 feedbackUv = rotate2d(fromCenter, feedbackAngle) * contraction;
    feedbackUv.x /= aspect;
    feedbackUv += ctr;

    // ── 2. SINGLE-OCTAVE CURL ADVECTION ─────────────────────────────────────
    // One octave: smooth organic drift. Bass widens the advection push.

    vec2 advP = (uv - 0.5) * 3.0;
    advP.x *= aspect;

    vec2 flow = curlNoise(advP, t * driftSpd);
    float flowMag = length(flow);
    float advStrength = 0.003 * (1.0 + (hasAudio ? bass * audioR * 0.5 : 0.0));
    vec2 advOffset = flow * advStrength;

    vec2 sampleUv = feedbackUv - advOffset;

    // ── 3. BASS RADIAL PULSES ───────────────────────────────────────────────
    // Expanding ring disturbances from zone centers on bass hits.
    // Pure math — no texture reads, no noise calls.

    float pulseEnergy = 0.0;
    if (hasAudio && bass > 0.08) {
        int n = min(zoneCount, 4);
        if (n == 0) n = 1;
        for (int ei = 0; ei < n && ei < 4; ei++) {
            vec2 eruptCtr = (zoneCount > 0) ? zoneCenter(ei, res) : vec2(0.5);
            vec2 toPixel = uv - eruptCtr;
            toPixel.x *= aspect;
            float dist = length(toPixel);

            float cycle = fract(t * (0.7 + float(ei) * 0.2));
            float ringRadius = cycle * 0.35;
            float ring = smoothstep(ringRadius + 0.03, ringRadius, dist)
                       * smoothstep(ringRadius - 0.03, ringRadius, dist)
                       * (1.0 - cycle);

            // Displace feedback sampling (distortion wave)
            vec2 dir = length(toPixel) > 1e-6 ? normalize(toPixel) : vec2(0.0);
            sampleUv += dir * ring * bass * audioR * 0.006;
            pulseEnergy += ring * bass * bass * audioR * 2.0;
        }
    }

    // ── Read previous frame ─────────────────────────────────────────────────
    vec4 prev = texture(iChannel0, channelUv(0, clamp(sampleUv, 0.0, 1.0) * res));
    float energy = prev.r * persistence;
    float heat   = prev.g * persistence * 0.93;

    // ── 4. ZONE-EDGE EMISSION ───────────────────────────────────────────────
    // Exponential falloff with noise flicker. Bass makes edges flare.

    float edgeDist = zoneEdgeSDF(fragCoord, pxs);

    if (edgeDist < edgeW * 2.0) {
        float falloff = exp(-edgeDist / max(edgeW, 1.0));

        float flicker = noise2D((uv - 0.5) * 6.0 + vec2(t * 0.4, t * -0.3));
        flicker = flicker * 0.7 + 0.3;

        float emission = falloff * flicker * intensity * 0.12;

        // Bass: edge emission surge
        if (hasAudio && bass > 0.05) {
            emission *= 1.0 + bass * audioR * 3.0;
        }

        // Mids: widen effective emission range (edges breathe with mid-range)
        if (hasAudio && mids > 0.05) {
            float midsBoost = exp(-edgeDist / max(edgeW * (1.0 + mids * audioR), 1.0));
            emission += midsBoost * mids * audioR * intensity * 0.03;
        }

        energy += emission;
        heat   += emission;
    }

    // ── 5. TREBLE SPARKS ────────────────────────────────────────────────────
    // Bright scattered points on treble — 1 noise2D call.

    float sparks = 0.0;
    if (hasAudio && treble > 0.06) {
        float sparkNoise = noise2D(uv * 40.0 + t * 4.0);
        float sparkThresh = 1.0 - treble * 0.25;
        sparks = smoothstep(sparkThresh, sparkThresh + 0.05, sparkNoise)
               * treble * audioR * 0.4;
    }

    // ── Combine ─────────────────────────────────────────────────────────────
    energy += pulseEnergy * 0.015 + sparks;
    heat   += pulseEnergy * 0.015 + sparks;

    // Soft saturation
    energy = energy / (1.0 + energy * 0.15);
    heat   = heat   / (1.0 + heat   * 0.25);

    float flowDir = clamp(flowMag * 0.3, 0.0, 1.0);

    fragColor = vec4(clamp(energy, 0.0, 1.0), clamp(heat, 0.0, 1.0), flowDir, 1.0);
}
