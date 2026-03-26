// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Ember Trace — Buffer Pass 0 (ping-pong feedback)
//
// A temporal fire simulation exploiting ping-pong feedback for effects impossible
// in single-frame shaders. Each frame reads its own previous output through
// iChannel0, applies a contracting+rotating transform (creating fractal spiral
// self-similarity), injects zone-edge fire and flowing plasma noise, then advects
// the whole field through multi-octave curl noise.
//
// Channel layout (consumed by effect pass):
//   R = energy intensity (brightness of fire, 0-1)
//   G = heat/freshness (1 = just injected, decays faster for color aging)
//   B = flow speed (curl noise magnitude, for motion-aware coloring)
//   A = 1

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <audio.glsl>

// ─── Parameters ─────────────────────────────────────────────────────────────

float getFireIntensity()   { return customParams[0].x >= 0.0 ? customParams[0].x : 0.8; }
float getTrailPersist()    { return customParams[0].y >= 0.0 ? customParams[0].y : 0.965; }
float getEdgeFireWidth()   { return customParams[0].z >= 0.0 ? customParams[0].z : 10.0; }
float getFeedbackStr()     { return customParams[0].w >= 0.0 ? customParams[0].w : 0.5; }
float getDriftSpeed()      { return customParams[1].x >= 0.0 ? customParams[1].x : 0.3; }
float getDriftScale()      { return customParams[1].y >= 0.0 ? customParams[1].y : 3.0; }
float getNoiseScale()      { return customParams[1].z >= 0.0 ? customParams[1].z : 4.0; }
float getAudioReact()      { return customParams[1].w >= 0.0 ? customParams[1].w : 1.0; }

// ─── Rotation helper ────────────────────────────────────────────────────────

vec2 rotate2d(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

// ─── Curl noise (multi-octave) for advection ────────────────────────────────

vec2 curlNoise(vec2 p, float t) {
    float eps = 0.5;
    float n  = noise2D(p + vec2(0.0, eps) + t);
    float ns = noise2D(p - vec2(0.0, eps) + t);
    float ne = noise2D(p + vec2(eps, 0.0) + t);
    float nw = noise2D(p - vec2(eps, 0.0) + t);
    return vec2(n - ns, -(ne - nw)) / (2.0 * eps);
}

vec2 fbmCurl(vec2 p, float t, int octaves) {
    vec2 flow = vec2(0.0);
    float amp = 1.0;
    float freq = 1.0;
    float c = cos(0.45), s = sin(0.45);
    mat2 rot = mat2(c, -s, s, c);
    for (int i = 0; i < octaves; i++) {
        flow += curlNoise(p * freq, t * (0.7 + float(i) * 0.25)) * amp;
        p = rot * p;
        freq *= 2.1;
        amp *= 0.48;
    }
    return flow;
}

// ─── FBM noise for plasma injection ─────────────────────────────────────────

float fbmNoise(vec2 p, float t, int octaves) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    float c = cos(0.6), s = sin(0.6);
    mat2 rot = mat2(c, -s, s, c);
    for (int i = 0; i < octaves; i++) {
        val += noise2D(p * freq + t * (0.3 + float(i) * 0.15)) * amp;
        p = rot * p;
        freq *= 2.0;
        amp *= 0.5;
    }
    return val;
}

// ─── Flowing plasma pattern (layered sinusoidal, like nexus-cascade) ────────

float plasmaPattern(vec2 uv, float t) {
    float v = 0.0;
    v += sin((uv.x + t * 0.6) * 7.0) * 0.25;
    v += sin((uv.y - t * 0.45) * 6.0) * 0.25;
    v += sin((uv.x * 3.0 + uv.y * 2.0 + t * 1.1) * 5.0) * 0.2;
    v += sin(length(uv) * 10.0 - t * 1.8) * 0.2;
    v += sin(atan(uv.y, uv.x) * 3.0 + t * 1.3) * 0.1;
    return v * 0.5 + 0.5;
}

// ─── Zone edge SDF: minimum signed distance to any zone border ──────────────

float zoneEdgeSDF(vec2 fragCoord) {
    float minDist = 1e6;
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec2 pos = zoneRectPos(zoneRects[i]);
        vec2 sz  = zoneRectSize(zoneRects[i]);
        float radius = zoneParams[i].x;
        vec2 center = pos + sz * 0.5;
        vec2 halfSz = sz * 0.5;
        float d = sdRoundedBox(fragCoord - center, halfSz, radius);
        minDist = min(minDist, abs(d));
    }
    return minDist;
}

// ─── Zone center positions (for bass eruptions) ─────────────────────────────

vec2 zoneCenter(int i, vec2 res) {
    vec2 pos = zoneRectPos(zoneRects[i]);
    vec2 sz  = zoneRectSize(zoneRects[i]);
    return (pos + sz * 0.5) / res;
}

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 res = max(iResolution.xy, vec2(1.0));
    vec2 uv = fragCoord / res;
    vec2 px = 1.0 / res;
    float aspect = res.x / res.y;

    // ── First frame: seed initial state ─────────────────────────────────────
    if (iFrame == 0) {
        // Sparse seed points to kick-start accumulation
        float energy = 0.0;
        for (int i = 0; i < 12; i++) {
            vec2 sp = hash22(vec2(float(i) * 13.7, float(i) * 7.3 + 42.0));
            vec2 diff = uv - sp;
            diff.x *= aspect;
            float d = length(diff);
            energy += smoothstep(0.04, 0.0, d) * (0.3 + hash11(float(i) * 3.1) * 0.4);
        }
        // Light zone-edge seeding on first frame
        float edgeDist = zoneEdgeSDF(fragCoord);
        energy += smoothstep(30.0, 0.0, edgeDist) * 0.2;
        energy = clamp(energy, 0.0, 1.0);
        fragColor = vec4(energy, energy, 0.0, 1.0);
        return;
    }

    // ── Parameters ──────────────────────────────────────────────────────────
    float fireIntensity = getFireIntensity();
    float persistence   = getTrailPersist();
    float edgeFireW     = getEdgeFireWidth();
    float feedbackStr   = getFeedbackStr();
    float driftSpd      = getDriftSpeed();
    float driftScl      = getDriftScale();
    float noiseScl      = getNoiseScale();
    float audioR        = getAudioReact();

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass     = getBassSoft();
    float mids     = getMidsSoft();
    float treble   = getTrebleSoft();

    float t = iTime;

    // ── 1. FEEDBACK TRANSFORM ───────────────────────────────────────────────
    // Read previous frame with slight contraction + rotation toward center.
    // This creates fractal spiral self-similarity: patterns appear to tunnel
    // inward while new material is injected at the edges.

    vec2 center = vec2(0.5);
    vec2 fromCenter = uv - center;
    fromCenter.x *= aspect;

    // Slow dreamy rotation, modulated by mids for phase shifting
    float baseRotation = t * 0.04;
    float midsPhaseShift = hasAudio ? mids * audioR * 0.15 : 0.0;
    float feedbackAngle = baseRotation + midsPhaseShift;

    // Slight contraction pulls everything inward (creates infinite tunnel)
    float contraction = 0.997 - clamp(feedbackStr, 0.0, 1.0) * 0.004;

    vec2 feedbackUv = rotate2d(fromCenter, feedbackAngle) * contraction;
    feedbackUv.x /= aspect;
    feedbackUv += center;

    // ── 2. CURL-NOISE ADVECTION ─────────────────────────────────────────────
    // Multi-octave curl noise drifts the field, creating organic swirling motion.

    vec2 advP = (uv - 0.5) * driftScl;
    advP.x *= aspect;

    int curlOctaves = 4;
    // Treble adds extra high-frequency curl octaves for turbulent mixing
    if (hasAudio && treble > 0.06) {
        curlOctaves = 5;
    }

    vec2 flow = fbmCurl(advP, t * driftSpd, curlOctaves);

    // Treble turbulence: additional high-frequency chaos
    if (hasAudio && treble > 0.06) {
        vec2 turb = curlNoise(advP * 5.0, iTime * 3.0);
        flow += turb * treble * audioR * 2.0;
    }

    float flowMag = length(flow);
    vec2 advOffset = flow * 0.003 * (1.0 + (hasAudio ? bass * audioR * 0.6 : 0.0));

    vec2 sampleUv = feedbackUv - advOffset;

    // ── 3. BASS ERUPTION SHOCKWAVES ─────────────────────────────────────────
    // Expanding ring disturbances from zone centers that shatter steady-state
    // feedback, creating temporary chaos like rocks thrown into fire-pools.

    vec2 shockDisplace = vec2(0.0);
    float shockEnergy = 0.0;

    if (hasAudio && bass > 0.08) {
        // Use zone centers as eruption origins, or drifting fallback points when no zones
        int eruptCount = (zoneCount > 0) ? min(zoneCount, 6) : 3;

        for (int ei = 0; ei < eruptCount && ei < 6; ei++) {
            vec2 eruptCenter;
            if (zoneCount > 0 && ei < zoneCount) {
                eruptCenter = zoneCenter(ei, res);
            } else {
                float drift = float(ei) * 2.094 + iTime * 0.12;
                eruptCenter = vec2(
                    0.5 + 0.3 * sin(drift * 0.7 + float(ei) * 1.3),
                    0.5 + 0.3 * cos(drift * 0.9 + float(ei) * 0.7)
                );
            }

            // Multiple expanding ring phases per eruption point
            for (int phase = 0; phase < 2; phase++) {
                float cycle = fract(iTime * (0.6 + float(ei) * 0.15) + float(phase) * 0.5);
                float ringRadius = cycle * 0.4;
                float ringLife = 1.0 - cycle; // fades as it expands

                vec2 toPixel = uv - eruptCenter;
                toPixel.x *= aspect;
                float dist = length(toPixel);

                float ringWidth = 0.03 + bass * 0.02;
                float ring = smoothstep(ringRadius + ringWidth, ringRadius, dist)
                           * smoothstep(ringRadius - ringWidth, ringRadius, dist);

                // Shockwave displaces the feedback sampling (distortion)
                vec2 dir = length(toPixel) > 1e-6 ? normalize(toPixel) : vec2(0.0);
                shockDisplace += dir * ring * bass * audioR * 0.008 * ringLife;
                shockEnergy += ring * bass * bass * audioR * 3.0 * ringLife;
            }
        }
    }

    sampleUv += shockDisplace;

    // ── Read previous frame with combined transform ─────────────────────────
    vec4 prev = texture(iChannel0, channelUv(0, clamp(sampleUv, 0.0, 1.0) * res));
    float energy = prev.r;
    float heat   = prev.g;

    // Apply feedback decay (trail persistence)
    // Energy decays slowly (long trails), heat decays faster (color aging)
    float heatDecay = persistence * 0.92; // heat fades 8% faster

    energy *= persistence;
    heat   *= heatDecay;

    // ── 4. ZONE-EDGE FIRE EMISSION ──────────────────────────────────────────
    // Fire emanates from zone borders, accumulating through feedback into
    // burning halos. Noise modulation creates flickering, organic edges.

    float edgeDist = zoneEdgeSDF(fragCoord);
    float edgeFire = 0.0;

    if (edgeDist < edgeFireW * 2.0) {
        // Base exponential falloff from edge
        float falloff = exp(-edgeDist / max(edgeFireW, 1.0));

        // Flowing noise modulation so fire flickers and crawls along edges
        float edgeNoise = fbmNoise(
            (uv - 0.5) * noiseScl + vec2(t * 0.3, t * -0.2),
            t * 0.5, 3
        );
        edgeNoise = edgeNoise * 0.8 + 0.2; // bias above zero

        // Secondary finer noise for flame tips
        float fineNoise = noise2D((uv - 0.5) * 12.0 + t * 0.8);
        fineNoise = fineNoise * 0.5 + 0.5;

        edgeFire = falloff * edgeNoise * fineNoise * fireIntensity * 0.09;

        // Bass makes zone edges flare brighter
        if (hasAudio && bass > 0.05) {
            edgeFire *= 1.0 + bass * audioR * 2.0;
        }
    }

    // ── 5. FLOWING PLASMA NOISE INJECTION ───────────────────────────────────
    // Small per-frame plasma contribution that accumulates through feedback
    // into rich, complex structures. NOT flat noise — layered flowing patterns.

    vec2 plasmaUv = (uv - 0.5) * driftScl * 0.8;
    plasmaUv.x *= aspect;
    plasmaUv = rotate2d(plasmaUv, t * 0.12);

    float plasma = plasmaPattern(plasmaUv, t);
    float fbm = fbmNoise(plasmaUv * 1.5, t * 0.4, 4);

    // Combine plasma + fbm — visible per-frame contribution (feedback accumulates it)
    float plasmaInject = (plasma * 0.5 + fbm * 0.5);
    plasmaInject *= plasmaInject; // square for contrast
    plasmaInject *= 0.028 * fireIntensity;

    // ── 6. MIDS = FEEDBACK PHASE SHIFT + SECONDARY LAYER ────────────────────
    // Mids modulate feedback rotation (done above) AND inject a secondary
    // pattern layer that reshapes the feedback topology.

    float midsLayer = 0.0;
    if (hasAudio && mids > 0.05) {
        vec2 midsUv = rotate2d((uv - 0.5) * 3.5, -t * 0.2 + mids * 2.0);
        midsUv.x *= aspect;
        float midsPattern = sin(midsUv.x * 6.0 + midsUv.y * 4.0 + t * 2.0)
                          * sin(midsUv.y * 5.0 - midsUv.x * 3.0 + t * 1.5);
        midsPattern = midsPattern * 0.5 + 0.5;
        midsPattern *= fbmNoise(midsUv, t * 0.6, 3);
        midsLayer = midsPattern * mids * audioR * 0.08;
    }

    // ── 7. TREBLE = SPARKS + TURBULENT MIXING ───────────────────────────────
    // High frequencies scatter bright spark points and add fractal chaos to
    // normally smooth fire edges.

    float sparks = 0.0;
    if (hasAudio && treble > 0.06) {
        // Scatter bright spark points (like cosmic-flow's stellar sparks)
        float sparkNoise = noise2D(uv * 50.0 + iTime * 5.0);
        float sparkThresh = 1.0 - treble * 0.3;
        sparks = smoothstep(sparkThresh, sparkThresh + 0.05, sparkNoise)
               * treble * audioR * 0.5;

        // Second layer of sparser, brighter sparks
        float bigSpark = noise2D(uv * 25.0 - iTime * 3.0 + 100.0);
        float bigThresh = 1.0 - treble * 0.15;
        sparks += smoothstep(bigThresh, bigThresh + 0.02, bigSpark)
                * treble * audioR * 0.8;

        // Turbulent diffusion: jitter the energy field by reading offset neighbors
        // This fractures smooth fire boundaries into flame-like chaos
        vec2 turbJitter = (hash22(uv * 200.0 + float(iFrame) * 1.618) - 0.5) * px * treble * audioR * 8.0;
        vec4 turbSample = texture(iChannel0, channelUv(0, clamp(sampleUv + turbJitter, 0.0, 1.0) * res));
        energy = mix(energy, turbSample.r, treble * audioR * 0.3);
    }

    // ── Combine all contributions ───────────────────────────────────────────

    float newEnergy = edgeFire + plasmaInject + midsLayer + shockEnergy * 0.02 + sparks;
    float newHeat = newEnergy; // fresh injection is maximally hot

    energy += newEnergy;
    heat   += newHeat;

    // Soft saturation (prevents harsh clipping, allows overbright to bloom naturally)
    energy = energy / (1.0 + energy * 0.2);
    heat   = heat   / (1.0 + heat   * 0.3);

    // Encode flow speed for motion-aware coloring in effect pass
    float flowSpeed = clamp(flowMag * 0.3, 0.0, 1.0);

    energy = clamp(energy, 0.0, 1.0);
    heat   = clamp(heat,   0.0, 1.0);

    fragColor = vec4(energy, heat, flowSpeed, 1.0);
}
