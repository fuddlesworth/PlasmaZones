// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Comet pointer shader — one bright head on the newest trail sample and a
// tapered single-colour tail along the trail polyline, with hashed grain
// twinkling through the tail. Coverage is the max over segments. The head
// itself fades with idle time so the whole comet is gone within the
// metadata's trailSeconds even while the pointer rests.
//
// A press makes the head flare and throws an extra helping of the same
// sparkle grain out from the press point, scaled by `clickBurst`. The burst
// is kept inside `width` of the press point so it stays within the damage
// rect the host derives from that parameter, and it is gone by
// kBurstSeconds, well inside trailSeconds.

#include <pointer_noise.glsl>

const int kMaxTrail = 32;
const float kTrailSeconds = 0.8;
const float kBurstSeconds = 0.35;

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
    // and is fully out by the end of the live window. It sits on the smoothed
    // path like the tail does, and the live pointer speed gates it, so below
    // the activation speed the whole comet is absent rather than leaving a
    // headlight behind.
    float headGate = pointerSpeedGate(uPointerVelocity.z, p_activationSpeed);
    vec2 headPos = pointerSmoothedAt(0, count, p_smoothing);
    float idle = pointerIdleSeconds();
    float headFade = (1.0 - smoothstep(0.35 * kTrailSeconds, kTrailSeconds, idle)) * headGate;
    float dHead = length(px - headPos);
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
        float gate = min(pointerSpeedGate(a.w, p_activationSpeed), pointerSpeedGate(b.w, p_activationSpeed));
        if (gate <= 0.0) {
            continue;
        }
        vec2 pa = pointerSmoothedAt(i, count, p_smoothing);
        vec2 pb = pointerSmoothedAt(i + 1, count, p_smoothing);
        vec2 lo = min(pa, pb) - cull;
        vec2 hi = max(pa, pb) + cull;
        if (px.x < lo.x || px.y < lo.y || px.x > hi.x || px.y > hi.y) {
            continue;
        }

        float t;
        float d = pointerSmoothSegmentDistance(px, i, count, p_smoothing, t);
        float age = clamp(mix(a.z, b.z, t) / tailSeconds, 0.0, 1.0);
        float life = (1.0 - age) * gate;
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

    // Click burst: the head flares and sheds a fistful of extra grain from
    // the press point. Everything is cut to zero at `reach` device px so the
    // burst cannot paint outside the declared damage rect. It is deliberately
    // outside the speed gate, so a click still answers when the pointer is
    // sitting still.
    float burst = 0.0;
    float sincePress = pointerSincePress();
    if (p_clickBurst > 0.0 && uPointerPress.w > 0.5 && sincePress < kBurstSeconds) {
        float ct = sincePress / kBurstSeconds;
        float decay = (1.0 - ct) * (1.0 - ct);
        float reach = max(p_width, 0.5) * scale;
        float dp = length(px - uPointerPress.xy);
        float ball = exp(-(dp * dp) / (2.0 * mix(radius * 0.5, reach * 0.45, ct) * mix(radius * 0.5, reach * 0.45, ct)));
        vec2 cell = floor(px / max(1.5 * scale, 1.0));
        float g = hash13(cell + floor(iTime * 20.0) * 0.71);
        float grain = smoothstep(0.55, 1.0, g);
        float edge = 1.0 - smoothstep(reach * 0.75, reach, dp);
        burst = decay * edge * (ball * 0.5 + ball * grain * 1.5) * p_clickBurst;
        // The head itself lifts with the burst rather than only the grain.
        headCore = min(headCore + decay * 0.4 * p_clickBurst * headCore, 1.0);
    }

    float alpha = clamp(headCore + headGlow + tail + tailGrain * p_sparkle + burst, 0.0, 1.0);
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    return premul(p_color.rgb, alpha * p_color.a);
}
