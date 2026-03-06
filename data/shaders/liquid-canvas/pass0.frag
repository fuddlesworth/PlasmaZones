// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#version 450

// Liquid Canvas -- Buffer Pass 0: Animated flow field / displacement map
// Outputs a 2D vector field encoded as RG (displacement direction + magnitude)
// and a scalar flow intensity in B channel. No channel inputs.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec2 vFragCoord;

layout(location = 0) out vec4 fragColor;

#include <common.glsl>
#include <audio.glsl>

// Curl noise from 2D value noise -- produces divergence-free flow
vec2 curlNoise(vec2 p, float t) {
    float eps = 0.5;
    float n  = noise2D(p + vec2(0.0, eps) + t);
    float ns = noise2D(p - vec2(0.0, eps) + t);
    float ne = noise2D(p + vec2(eps, 0.0) + t);
    float nw = noise2D(p - vec2(eps, 0.0) + t);
    return vec2(n - ns, -(ne - nw)) / (2.0 * eps);
}

// Multi-octave curl noise for complex, organic flow
vec2 fbmCurl(vec2 p, float t, int octaves) {
    vec2 flow = vec2(0.0);
    float amp = 1.0;
    float freq = 1.0;
    float c = cos(0.4), s = sin(0.4);
    mat2 rot = mat2(c, -s, s, c);
    for (int i = 0; i < octaves && i < 8; i++) {
        flow += curlNoise(p * freq, t * (0.8 + float(i) * 0.15)) * amp;
        p = rot * p;
        freq *= 2.0;
        amp *= 0.55;
    }
    return flow;
}

void main() {
    vec2 fragCoord = vFragCoord;
    vec2 uv = fragCoord / max(iResolution.xy, vec2(1.0));

    float speed     = customParams[0].x >= 0.0 ? customParams[0].x : 0.3;
    float scale     = customParams[0].y >= 0.0 ? customParams[0].y : 3.0;
    int   octaves   = int(customParams[0].z >= 0.0 ? customParams[0].z : 5.0);
    float audioReact = customParams[0].w >= 0.0 ? customParams[0].w : 1.0;

    bool  hasAudio = iAudioSpectrumSize > 0;
    float bass     = getBassSoft();
    float mids     = getMidsSoft();
    float treble   = getTrebleSoft();

    float t = iTime * speed;

    // Aspect-correct UV for flow field sampling
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    vec2 p = (uv - 0.5) * scale;
    p.x *= aspect;

    // Base multi-octave curl flow
    vec2 flow = fbmCurl(p, t, octaves);

    // -- Bass: concentric ripple pulses from center --
    // Bass creates expanding ring disturbances in the flow field,
    // like dropping stones into still water.
    if (hasAudio && bass > 0.04) {
        float bassSquared = bass * bass;
        // Aspect-correct distance so ripples are circular, not elliptical
        vec2 centered = uv - 0.5;
        centered.x *= aspect;
        for (int ri = 0; ri < 3; ri++) {
            // Each ripple has a different expansion rate
            float cycle = fract(iTime * (0.6 + float(ri) * 0.25));
            float radius = cycle * 1.5; // expands outward in UV space
            float dist = length(centered) * 2.0;
            // Ring-shaped wavefront
            float ring = exp(-abs(dist - radius) * 8.0) * (1.0 - cycle);
            // Radial push direction
            vec2 radial = normalize(centered + 0.0001);
            flow += radial * ring * bassSquared * audioReact * 2.0;
        }
    }

    // -- Mids: directional current shift --
    // Mids rotate the dominant flow direction, creating visible current changes.
    if (hasAudio && mids > 0.03) {
        float midsAngle = mids * audioReact * 0.8;
        float cs = cos(midsAngle), sn = sin(midsAngle);
        flow = mat2(cs, -sn, sn, cs) * flow;
        // Add gentle uniform current in shifted direction
        flow += vec2(cs, sn) * mids * audioReact * 0.3;
    }

    // -- Treble: high-frequency turbulence --
    // Treble injects small-scale noise turbulence at the edges of flow patterns.
    if (hasAudio && treble > 0.06) {
        vec2 turbulence = curlNoise(p * 4.0, iTime * 2.0) * treble * audioReact * 0.5;
        flow += turbulence;
    }

    // Encode: RG = flow direction (remapped to 0-1), B = flow magnitude, A = 1
    float mag = length(flow);
    vec2 dir = mag > 0.001 ? flow / mag : vec2(0.0);
    fragColor = vec4(
        dir * 0.5 + 0.5,   // RG: direction remapped from [-1,1] to [0,1]
        mag * 0.5,          // B: magnitude (scaled for storage)
        1.0
    );
}
