// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Arc pointer shader — short jagged lightning arcs that snap between pairs of
// points on the pointer's recent path. Every arc is a polyline of kSegments
// straight pieces between two trail samples, each interior vertex pushed off
// the straight line by a hashed amount, so the arc reads as a crooked spark
// rather than a curve.
//
// DETERMINISM: an arc's endpoints and its whole jag pattern come from a hash
// of the ROLL INDEX, floor(iTime * rate), and the arc index. Every frame
// inside one roll window therefore draws the identical arc and the pack
// crackles at `rate` re-rolls a second instead of strobing at the frame rate.
//
// Arcs are driven by speed: at rest only the first arc is lit and it is dim,
// and the full set comes in as the pointer moves. The count fades in
// fractionally so an arc appears by brightening rather than popping.
//
// A press throws `clickBurst` extra arcs radiating OUT from the press point,
// each one striking from the press point to a jittered direction that
// lengthens as the burst ages. It is a fan, not a ring: the expanding ring
// belongs to Click Ripple.
//
// FADE: everything is multiplied by an idle envelope that reaches exactly
// zero at kQuietSeconds, and the click burst dies at kBurstLife. Both are
// below the metadata trailSeconds, so nothing is left on screen when the host
// stops asking for frames.

#include <pointer_noise.glsl>

const int kMaxArcs = 8;
const int kSegments = 8;
const float kQuietSeconds = 0.5;
const float kBurstLife = 0.34;
// Speed (device px/s) at which the pack is fully awake. Deliberately low: the
// settings preview's simulated pointer peaks near 324 px/s.
const float kFullSpeed = 200.0;

// Distance from p to the segment a..b. Local to this pack: the shared
// pointerSegmentDistance works on trail indices, and an arc's vertices are
// hashed points that are not on the path.
float arcSegmentDistance(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 < 1e-6) {
        return length(p - a);
    }
    float t = clamp(dot(p - a, ab) / len2, 0.0, 1.0);
    return length(p - (a + ab * t));
}

// Nearest distance from p to the jagged polyline a..b. `seed` fixes the jag
// pattern, `amp` is the maximum sideways push in device px.
float arcDistance(vec2 p, vec2 a, vec2 b, vec2 seed, float amp) {
    vec2 dir = b - a;
    float len = length(dir);
    if (len < 1e-4) {
        return length(p - a);
    }
    vec2 perp = vec2(-dir.y, dir.x) / len;
    vec2 prev = a;
    float best = 1e9;
    for (int k = 1; k <= kSegments; ++k) {
        float t = float(k) / float(kSegments);
        vec2 base = a + dir * t;
        // Pinned at both ends, widest in the middle.
        float taper = sin(t * 3.14159265);
        float j = hash13(seed + vec2(float(k) * 13.7, 4.3)) - 0.5;
        vec2 v = (k == kSegments) ? b : base + perp * (j * 2.0 * amp * taper);
        best = min(best, arcSegmentDistance(p, prev, v));
        prev = v;
    }
    return best;
}

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 1) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float reachPx = max(p_reach, 4.0) * scale;
    float rate = clamp(p_crackleRate, 1.0, 60.0);
    float jag = clamp(p_jaggedness, 0.0, 1.0);
    float intensity = max(p_intensity, 0.0);

    // Idle envelope: exactly zero once the pointer has been still for
    // kQuietSeconds, which is inside the metadata trailSeconds.
    float live = clamp(1.0 - pointerIdleSeconds() / kQuietSeconds, 0.0, 1.0);
    float speed = uPointerVelocity.z;
    float activity = clamp(speed / kFullSpeed, 0.0, 1.0);
    float gate = pointerSpeedGate(speed, p_activationSpeed);

    float roll = floor(iTime * rate);
    float phase = clamp(fract(iTime * rate), 0.0, 1.0);
    // Sharp attack, straight decay: the arc strikes and dies inside its window.
    float strike = smoothstep(0.0, 0.12, phase) * (1.0 - phase);

    float core = 1.1 * scale;
    float glow = 5.5 * scale;

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;

    // Number of lit arcs grows with speed; the last one fades in fractionally.
    float budget = 1.0 + (clamp(float(int(p_arcs + 0.5)), 1.0, float(kMaxArcs)) - 1.0) * activity;
    float arcAlpha = live * gate * strike * intensity * (0.35 + 0.65 * activity);
    if (arcAlpha > 0.0 && count >= 2) {
        for (int j = 0; j < kMaxArcs; ++j) {
            if (float(j) >= budget) {
                break;
            }
            float share = clamp(budget - float(j), 0.0, 1.0);
            vec2 seed = vec2(roll * 1.731 + float(j) * 37.19, float(j) * 11.53 + 5.0);
            vec3 h = hash23(seed);

            int last = count - 1;
            int i0 = int(h.x * float(last) * 0.999);
            int span = 1 + int(h.y * 3.999);
            int i1 = min(i0 + span, last);
            vec2 a = pointerTrailAt(i0).xy;
            vec2 b = pointerTrailAt(i1).xy;
            vec2 d = b - a;
            float len = length(d);
            if (len > reachPx) {
                b = a + d * (reachPx / len);
                len = reachPx;
            }
            if (len < 1.0) {
                // A stationary pair has no arc to draw; stretching it into one
                // would leave a stub sitting under a parked pointer.
                continue;
            }
            float amp = jag * 0.22 * len;
            // Cheap rejection before the polyline walk.
            vec2 lo = min(a, b) - vec2(amp + glow * 3.0);
            vec2 hi = max(a, b) + vec2(amp + glow * 3.0);
            if (any(lessThan(px, lo)) || any(greaterThan(px, hi))) {
                continue;
            }

            float dist = arcDistance(px, a, b, seed, amp);
            float hot = exp(-(dist * dist) / (2.0 * core * core));
            float halo = exp(-(dist * dist) / (2.0 * glow * glow)) * 0.55;
            float cover = clamp(hot + halo, 0.0, 1.0) * arcAlpha * share * (0.6 + 0.4 * h.z);
            if (cover <= 0.0) {
                continue;
            }
            vec3 tint = mix(p_color.rgb, vec3(1.0), hot * 0.85);
            float ca = cover * p_color.a;
            rgb += tint * ca;
            alpha += ca;
        }
    }

    // ── click burst ──
    float sincePress = pointerSincePress();
    int burst = clamp(int(p_clickBurst + 0.5), 0, kMaxArcs);
    if (burst > 0 && uPointerPress.w > 0.5 && sincePress < kBurstLife) {
        vec2 origin = uPointerPress.xy;
        float k = sincePress / kBurstLife;
        float remain = 1.0 - k;
        // The strike reaches out over the burst's life and dims as it goes.
        float len = reachPx * (0.35 + 0.65 * k);
        float burstAlpha = remain * remain * intensity;
        for (int j = 0; j < kMaxArcs; ++j) {
            if (j >= burst) {
                break;
            }
            vec3 h = hash23(vec2(float(j) * 21.7 + 3.0, floor(origin.x) * 0.13 + floor(origin.y) * 0.29));
            float angle = (float(j) + 0.4 * h.x) / float(burst) * TAU;
            vec2 tip = origin + vec2(cos(angle), sin(angle)) * len * (0.55 + 0.45 * h.y);
            float amp = jag * 0.22 * length(tip - origin);
            vec2 lo = min(origin, tip) - vec2(amp + glow * 3.0);
            vec2 hi = max(origin, tip) + vec2(amp + glow * 3.0);
            if (any(lessThan(px, lo)) || any(greaterThan(px, hi))) {
                continue;
            }
            vec2 seed = vec2(roll * 0.917 + float(j) * 5.31, float(j) * 19.7 + 61.0);
            float dist = arcDistance(px, origin, tip, seed, amp);
            float hot = exp(-(dist * dist) / (2.0 * core * core));
            float halo = exp(-(dist * dist) / (2.0 * glow * glow)) * 0.55;
            float cover = clamp(hot + halo, 0.0, 1.0) * burstAlpha * (0.6 + 0.4 * h.z);
            if (cover <= 0.0) {
                continue;
            }
            vec3 tint = mix(p_color.rgb, vec3(1.0), hot * 0.85);
            float ca = cover * p_color.a;
            rgb += tint * ca;
            alpha += ca;
        }
    }

    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    float clamped = min(alpha, 1.0);
    return vec4(rgb * (clamped / alpha), clamped);
}
