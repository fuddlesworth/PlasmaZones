// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Comet pointer shader — one bright head on the newest trail sample and a
// tapered single-colour tail along the trail polyline, with hashed grain
// twinkling through the tail. Coverage is the max over segments. The head
// itself fades with idle time so the whole comet is gone within the
// metadata's trailSeconds even while the pointer rests.

#include <pointer_noise.glsl>

const int kMaxTrail = 32;
const float kTrailSeconds = 0.8;

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 1) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float tailSeconds = clamp(p_length, 0.05, kTrailSeconds);
    float radius = 0.5 * max(p_width, 0.5) * scale;
    float cull = radius * 4.0;

    // Head: a soft dot on the newest sample that dims as the pointer idles,
    // and is fully out by the end of the live window.
    vec4 head = pointerTrailAt(0);
    float idle = pointerIdleSeconds();
    float headFade = 1.0 - smoothstep(0.35 * kTrailSeconds, kTrailSeconds, idle);
    float dHead = length(px - head.xy);
    float headCore = (1.0 - smoothstep(radius - 0.75, radius + 0.75, dHead)) * headFade;
    float headGlow = exp(-(dHead * dHead) / (2.0 * radius * radius * 2.25)) * 0.5 * headFade;

    float tail = 0.0;
    float tailGrain = 0.0;
    for (int i = 0; i < kMaxTrail - 1; ++i) {
        if (i + 1 >= count) {
            break;
        }
        vec4 a = pointerTrailAt(i);
        vec4 b = pointerTrailAt(i + 1);
        if (a.z >= tailSeconds) {
            break;
        }
        vec2 lo = min(a.xy, b.xy) - cull;
        vec2 hi = max(a.xy, b.xy) + cull;
        if (px.x < lo.x || px.y < lo.y || px.x > hi.x || px.y > hi.y) {
            continue;
        }

        float t;
        float d = pointerSegmentDistance(px, i, t);
        float age = clamp(mix(a.z, b.z, t) / tailSeconds, 0.0, 1.0);
        float life = 1.0 - age;
        float w = radius * life;
        float body = (1.0 - smoothstep(w - 0.75, w + 0.75, d)) * life * life;
        float soft = exp(-(d * d) / (2.0 * (w + scale) * (w + scale) * 4.0)) * 0.3 * life * life;
        float here = max(body, soft);
        if (here > tail) {
            tail = here;
            // Grain that lives in the tail's own frame: cells hashed by the
            // path position and a slow time step so it twinkles rather than
            // crawls.
            vec2 cell = floor(px / max(1.5 * scale, 1.0));
            float g = hash13(cell + floor(iTime * 12.0) * 0.37);
            tailGrain = smoothstep(0.75, 1.0, g) * soft * 3.0;
        }
    }

    float alpha = clamp(headCore + headGlow + tail + tailGrain * p_sparkle, 0.0, 1.0);
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    return premul(p_color.rgb, alpha * p_color.a);
}
