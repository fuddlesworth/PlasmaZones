// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fireflies surface shader — a small swarm of soft sparks drifting around
// the window in the padded outer margin (this pack declares
// `paddingParam: flyRange`, so the host inflates the capture canvas the
// same way it does for the glow pack). Each spark gets hash-derived orbit
// speed, direction, radial breathing, blink rhythm and colour mix, so the
// swarm reads organic without any per-frame state. Sparks composite
// BEHIND the window (over transparency only), so they duck under the
// frame edge instead of crossing the content. Dims when unfocused.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

#version 450
#include <surface_lib.glsl>
#include <surface_noise.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const int kMaxFlies = 16;

void main() {
    vec4 window = surfaceTexel(vTexCoord);

    if (surfaceFrameDegenerate()) {
        fragColor = window;
        return;
    }

    vec2 px = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 cen = uSurfaceFrameTopLeft + halfSz;

    float reach = max(p_flyRange, 4.0) * uSurfaceScale;
    float size = max(p_flySize, 0.5) * max(uSurfaceScale, 0.001);
    float t = iTime * max(p_driftSpeed, 0.0);
    float count = clamp(p_flyCount, 1.0, float(kMaxFlies));

    vec3 glow = vec3(0.0);
    float alpha = 0.0;
    for (int i = 0; i < kMaxFlies; ++i) {
        if (float(i) >= count) {
            break;
        }
        float h1 = hashSin1(float(i) + 0.13);
        float h2 = hashSin1(float(i) + 7.71);
        float h3 = hashSin1(float(i) + 42.9);

        // Slow orbit around the frame, direction and rate per spark, with
        // the radial distance breathing between the frame edge and the
        // margin's reach so paths interleave instead of forming a ring.
        float dir = h3 > 0.5 ? 1.0 : -1.0;
        float ang = TAU * fract(h1 + dir * t * (0.02 + 0.035 * h2));
        float off = reach * (0.25 + 0.6 * (0.5 + 0.5 * sin(t * (0.5 + 0.8 * h2) + h1 * TAU)));
        vec2 pos = cen + vec2(cos(ang) * (halfSz.x + off), sin(ang) * (halfSz.y + off));

        // Soft gaussian body with a per-spark blink (cubed sine reads as a
        // firefly's pulse: mostly dim with bright peaks).
        float dist = length(px - pos);
        float body = exp(-dist * dist / (2.0 * size * size));
        float blinkWave = 0.5 + 0.5 * sin(t * (1.2 + 2.0 * h1) + h2 * TAU);
        float blink = 0.25 + 0.75 * blinkWave * blinkWave * blinkWave;

        vec4 col = mix(p_colorA, p_colorB, h3);
        glow += col.rgb * body * blink * col.a;
        alpha += body * blink * col.a;
    }
    alpha = clamp(alpha, 0.0, 1.0);

    // Focus cue: the swarm dims on unfocused surfaces, like the border family.
    float dim = focusDim(0.55);
    vec4 pane = vec4(glow * dim, alpha * dim);

    // Behind-the-window composite: sparks light only the transparent
    // margin (and any translucency), never crossing the content.
    fragColor = slabComposite(window, pane);
}
