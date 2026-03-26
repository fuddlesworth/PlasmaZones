// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Neon Phantom — Buffer Pass 0 (ping-pong feedback)
//
// Ghosted neon energy field simulation. Each frame reads its own previous output,
// applies a spiraling contraction with hexagonal lattice injection and octahedral
// interference patterns. Zone edges emit phantom energy that accumulates through
// feedback into spectral hex grids. Curl noise advects the field with cyberpunk
// holographic distortion.
//
// Channel layout (consumed by effect pass):
//   R = phantom energy intensity (0-1)
//   G = freshness/heat (1 = just emitted, decays for color shifting)
//   B = hex proximity (encodes distance to hex lattice for structural coloring)
//   A = 1

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <multipass.glsl>
#include <audio.glsl>

// ─── Parameters ─────────────────────────────────────────────────────────────

float getPhantomIntensity() { return customParams[0].x >= 0.0 ? customParams[0].x : 0.7; }
float getTrailPersist()     { return customParams[0].y >= 0.0 ? customParams[0].y : 0.97; }
float getEdgeGlowWidth()    { return customParams[0].z >= 0.0 ? customParams[0].z : 12.0; }
float getFeedbackStr()      { return customParams[0].w >= 0.0 ? customParams[0].w : 0.6; }
float getDriftSpeed()       { return customParams[1].x >= 0.0 ? customParams[1].x : 0.25; }
float getHexScale()         { return customParams[1].y >= 0.0 ? customParams[1].y : 5.0; }
float getNoiseScale()       { return customParams[1].z >= 0.0 ? customParams[1].z : 4.0; }
float getAudioReact()       { return customParams[1].w >= 0.0 ? customParams[1].w : 1.0; }

// ─── Rotation helper ────────────────────────────────────────────────────────

vec2 rotate2d(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

// ─── Hexagonal lattice distance ─────────────────────────────────────────────
// Returns distance to nearest hex center and the hex cell ID.

vec3 hexDist(vec2 p) {
    // Axial hex grid
    vec2 s = vec2(1.0, 1.7320508); // 1, sqrt(3)
    vec4 hC = floor(vec4(p, p - vec2(0.5, 1.0)) / s.xyxy) + 0.5;
    vec4 hF = vec4(p - hC.xy * s, p - (hC.zw + 0.5) * s);
    vec4 hD = vec4(dot(hF.xy, hF.xy), dot(hF.zw, hF.zw), 0.0, 0.0);
    vec2 nearest = hD.x < hD.y ? hC.xy : hC.zw + 0.5;
    float d = sqrt(min(hD.x, hD.y));
    return vec3(d, nearest);
}

// ─── Octahedral interference pattern ────────────────────────────────────────
// 2D approximation of the 3D octahedron SDF — creates diamond/octahedral tiling

float octaPattern(vec2 p) {
    p = abs(p);
    return (p.x + p.y);
}

// ─── Curl noise for advection ───────────────────────────────────────────────

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

// ─── FBM noise ──────────────────────────────────────────────────────────────

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

// ─── Ghosted energy pattern (hex + octahedral interference) ─────────────────
// Layered sinusoidal patterns shaped by hexagonal and octahedral geometry

float phantomPattern(vec2 uv, float t, float hexScl) {
    // Hex lattice energy
    vec3 hex = hexDist(uv * hexScl);
    float hexEdge = smoothstep(0.5, 0.2, hex.x); // bright at hex edges
    float hexCenter = smoothstep(0.0, 0.3, hex.x); // dark at hex centers

    // Octahedral interference (diamond grid overlaid)
    vec2 octUv = uv * hexScl * 0.7 + t * 0.1;
    float oct = fract(octaPattern(octUv) * 0.5);
    float octGrid = smoothstep(0.45, 0.5, oct) + smoothstep(0.55, 0.5, oct);

    // Flowing energy underneath
    float flow = 0.0;
    flow += sin((uv.x + t * 0.5) * 8.0) * 0.2;
    flow += sin((uv.y - t * 0.35) * 7.0) * 0.2;
    flow += sin((uv.x * 3.0 + uv.y * 2.0 + t * 0.9) * 5.0) * 0.15;
    flow += sin(length(uv) * 12.0 - t * 1.5) * 0.15;
    flow = flow * 0.5 + 0.5;

    // Combine: hex structure modulates flowing energy
    return (hexEdge * 0.5 + octGrid * 0.2 + flow * 0.3) * hexCenter;
}

// ─── Zone edge SDF ──────────────────────────────────────────────────────────

float zoneEdgeSDF(vec2 fragCoord) {
    float minDist = 1e6;
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec2 pos = zoneRectPos(zoneRects[i]);
        vec2 sz  = zoneRectSize(zoneRects[i]);
        float radius = zoneParams[i].x;
        vec2 center = pos + sz * 0.5;
        float d = sdRoundedBox(fragCoord - center, sz * 0.5, radius);
        minDist = min(minDist, abs(d));
    }
    return minDist;
}

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
        float energy = 0.0;
        // Seed with hex lattice pattern
        vec3 hex = hexDist((uv - 0.5) * getHexScale());
        energy += smoothstep(0.4, 0.15, hex.x) * 0.3;
        // Zone edge seeding
        float edgeDist = zoneEdgeSDF(fragCoord);
        energy += smoothstep(30.0, 0.0, edgeDist) * 0.25;
        energy = clamp(energy, 0.0, 1.0);
        fragColor = vec4(energy, energy, 0.5, 1.0);
        return;
    }

    // ── Parameters ──────────────────────────────────────────────────────────
    float phantomInt  = getPhantomIntensity();
    float persistence = getTrailPersist();
    float edgeGlowW   = getEdgeGlowWidth();
    float feedbackStr = getFeedbackStr();
    float driftSpd    = getDriftSpeed();
    float hexScl      = getHexScale();
    float noiseScl    = getNoiseScale();
    float audioR      = getAudioReact();

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass     = getBassSoft();
    float mids     = getMidsSoft();
    float treble   = getTrebleSoft();

    float t = iTime;

    // ── 1. FEEDBACK TRANSFORM ───────────────────────────────────────────────
    // Spiraling contraction with holographic wobble — creates the "ghosted"
    // energy tunnel effect from the original neon_phantom ray march.

    vec2 center = vec2(0.5);
    vec2 fromCenter = uv - center;
    fromCenter.x *= aspect;

    // Slow spectral rotation + holographic warp
    float baseRotation = t * 0.035;
    float holoWarp = sin(uv.y * 10.0 + t * 2.0) * 0.003; // neon_phantom holographic distortion
    float midsPhaseShift = hasAudio ? mids * audioR * 0.12 : 0.0;
    float feedbackAngle = baseRotation + midsPhaseShift;

    float contraction = 0.998 - feedbackStr * 0.003;

    vec2 feedbackUv = rotate2d(fromCenter, feedbackAngle) * contraction;
    feedbackUv.x += holoWarp;
    feedbackUv.x /= aspect;
    feedbackUv += center;

    // ── 2. CURL-NOISE ADVECTION ─────────────────────────────────────────────

    vec2 advP = (uv - 0.5) * noiseScl * 0.8;
    advP.x *= aspect;

    int curlOctaves = 4;
    if (hasAudio && treble > 0.06) curlOctaves = 5;

    vec2 flow = fbmCurl(advP, t * driftSpd, curlOctaves);

    // Treble turbulence
    if (hasAudio && treble > 0.06) {
        vec2 turb = curlNoise(advP * 5.0, t * 3.0);
        flow += turb * treble * audioR * 1.5;
    }

    float flowMag = length(flow);
    vec2 advOffset = flow * 0.0025 * (1.0 + (hasAudio ? bass * audioR * 0.5 : 0.0));

    vec2 sampleUv = feedbackUv - advOffset;

    // ── 3. BASS SHOCKWAVES (phantom eruptions) ──────────────────────────────

    vec2 shockDisplace = vec2(0.0);
    float shockEnergy = 0.0;

    if (hasAudio && bass > 0.08) {
        int eruptCount = min(zoneCount, 6);
        if (eruptCount == 0) eruptCount = 3;

        for (int ei = 0; ei < eruptCount && ei < 6; ei++) {
            vec2 eruptCenter;
            if (zoneCount > 0) {
                eruptCenter = zoneCenter(ei, res);
            } else {
                float drift = float(ei) * 2.094 + t * 0.12;
                eruptCenter = vec2(
                    0.5 + 0.3 * sin(drift * 0.7 + float(ei) * 1.3),
                    0.5 + 0.3 * cos(drift * 0.9 + float(ei) * 0.7)
                );
            }

            for (int phase = 0; phase < 2; phase++) {
                float cycle = fract(t * (0.5 + float(ei) * 0.12) + float(phase) * 0.5);
                float ringRadius = cycle * 0.35;
                float ringLife = 1.0 - cycle;

                vec2 toPixel = uv - eruptCenter;
                toPixel.x *= aspect;
                float dist = length(toPixel);

                float ringWidth = 0.025 + bass * 0.015;
                float ring = smoothstep(ringRadius + ringWidth, ringRadius, dist)
                           * smoothstep(ringRadius - ringWidth, ringRadius, dist);

                vec2 dir = normalize(toPixel + 0.0001);
                shockDisplace += dir * ring * bass * audioR * 0.006 * ringLife;
                shockEnergy += ring * bass * bass * audioR * 2.5 * ringLife;
            }
        }
    }

    sampleUv += shockDisplace;

    // ── Read previous frame ─────────────────────────────────────────────────
    vec4 prev = texture(iChannel0, channelUv(0, clamp(sampleUv, 0.0, 1.0) * res));
    float energy = prev.r;
    float heat   = prev.g;

    float heatDecay = persistence * 0.91;
    energy *= persistence;
    heat   *= heatDecay;

    // ── 4. ZONE-EDGE PHANTOM EMISSION ───────────────────────────────────────
    // Neon phantom energy emanates from zone borders, modulated by hex lattice

    float edgeDist = zoneEdgeSDF(fragCoord);
    float edgeEmit = 0.0;

    if (edgeDist < edgeGlowW * 2.0) {
        float falloff = exp(-edgeDist / max(edgeGlowW, 1.0));

        // Hex-modulated edge emission — phantom energy follows the lattice
        vec3 hex = hexDist((uv - 0.5) * hexScl + vec2(t * 0.15, t * -0.1));
        float hexMod = smoothstep(0.5, 0.15, hex.x) * 0.6 + 0.4;

        float edgeNoise = fbmNoise(
            (uv - 0.5) * noiseScl + vec2(t * 0.25, t * -0.15),
            t * 0.4, 3
        );
        edgeNoise = edgeNoise * 0.8 + 0.2;

        edgeEmit = falloff * edgeNoise * hexMod * phantomInt * 0.08;

        if (hasAudio && bass > 0.05) {
            edgeEmit *= 1.0 + bass * audioR * 1.8;
        }
    }

    // ── 5. PHANTOM PATTERN INJECTION ────────────────────────────────────────
    // Hex + octahedral energy patterns that accumulate into spectral grids

    vec2 patternUv = (uv - 0.5) * noiseScl * 0.7;
    patternUv.x *= aspect;
    patternUv = rotate2d(patternUv, t * 0.1);

    float phantom = phantomPattern(patternUv, t, hexScl * 0.6);
    float fbm = fbmNoise(patternUv * 1.2, t * 0.35, 4);

    float phantomInject = (phantom * 0.6 + fbm * 0.4);
    phantomInject *= phantomInject;
    phantomInject *= 0.025 * phantomInt;

    // ── 6. MIDS = HEX LATTICE TOPOLOGY SHIFT ───────────────────────────────

    float midsLayer = 0.0;
    if (hasAudio && mids > 0.05) {
        vec2 midsUv = rotate2d((uv - 0.5) * 3.0, -t * 0.15 + mids * 1.5);
        midsUv.x *= aspect;

        // Hex lattice at different scale — mids morph the structure
        vec3 midsHex = hexDist(midsUv * hexScl * 0.5 + t * 0.2);
        float midsPattern = smoothstep(0.4, 0.1, midsHex.x);
        midsPattern *= fbmNoise(midsUv, t * 0.5, 3);
        midsLayer = midsPattern * mids * audioR * 0.07;
    }

    // ── 7. TREBLE = GLITCH SPARKS + DATA FLOW ───────────────────────────────

    float sparks = 0.0;
    if (hasAudio && treble > 0.06) {
        // Glitch spark points (cyberpunk aesthetic)
        float sparkNoise = noise2D(uv * 50.0 + t * 5.0);
        float sparkThresh = 1.0 - treble * 0.25;
        sparks = smoothstep(sparkThresh, sparkThresh + 0.05, sparkNoise)
               * treble * audioR * 0.4;

        // Data flow lines (from neon_phantom)
        float dataFlow = sin(uv.x * 40.0 + t * 2.0) * sin(uv.y * 10.0);
        dataFlow = smoothstep(0.75, 1.0, dataFlow);
        sparks += dataFlow * treble * audioR * 0.15;

        // Turbulent diffusion
        vec2 turbJitter = (hash22(uv * 200.0 + t) - 0.5) * px * treble * audioR * 6.0;
        vec4 turbSample = texture(iChannel0, channelUv(0, clamp(sampleUv + turbJitter, 0.0, 1.0) * res));
        energy = mix(energy, turbSample.r, treble * audioR * 0.25);
    }

    // ── Combine all contributions ───────────────────────────────────────────

    float newEnergy = edgeEmit + phantomInject + midsLayer + shockEnergy * 0.015 + sparks;
    float newHeat = newEnergy;

    energy += newEnergy;
    heat   += newHeat;

    // Soft saturation
    energy = energy / (1.0 + energy * 0.2);
    heat   = heat   / (1.0 + heat   * 0.3);

    // Hex proximity for structural coloring in effect pass
    vec3 hexInfo = hexDist((uv - 0.5) * hexScl);
    float hexProximity = clamp(1.0 - hexInfo.x * 2.0, 0.0, 1.0);

    energy = clamp(energy, 0.0, 1.0);
    heat   = clamp(heat,   0.0, 1.0);

    fragColor = vec4(energy, heat, hexProximity, 1.0);
}
