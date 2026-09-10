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
// Arcs are driven by speed: at low speed only the first arc is lit and it is
// dim, the full set comes in as the pointer speeds up, and at rest they go
// out within kQuietSeconds. The count fades in fractionally so an arc
// appears by brightening rather than popping.
//
// A press throws `clickBurst` extra arcs radiating OUT from the press point,
// each one striking from the press point to a jittered direction that
// lengthens as the burst ages. It is a fan, not a ring: the expanding ring
// belongs to Click Ripple.
//
// FADE: the trail arcs are multiplied by an idle envelope that reaches
// exactly zero at kQuietSeconds, and the click burst dies at kBurstLife on
// its own clock (a click on a parked pointer still bursts). Both are below
// the metadata trailSeconds, so nothing is left on screen when the host
// stops asking for frames.
//
// ENDPOINTS come only from samples younger than kQuietSeconds. The ring is
// never purged while the pointer rests and it is spread over the longest
// window of the whole chain, so after a pause, or beside a longer-lived pack,
// most of its slots hold positions the pointer left seconds ago. An arc
// hashed onto one of those would strike along a path the user has moved on
// from, or, in a chain of this pack alone, be cut off at the edge of the
// damage rect, which covers only the samples inside the chain's longest
// window.
//
// REACH. Every arc's whole excursion, the segment plus its jag amplitude
// plus three glow sigmas of halo, is held inside the reach so nothing meets
// the damage rect's edge at a visible level. The budget the excursion is
// fitted to is floored at 35 percent of the reach, as Burst floors its
// travel: at the smallest reach the halo alone is wider than the reach, and
// without the floor every strike would collapse onto its origin.

#include <pointer_noise.glsl>

const int kMaxArcs = 8;
const int kSegments = 8;
const float kQuietSeconds = 0.5;
const float kBurstLife = 0.34;
// Speed (logical px/s, scaled at use) at which the pack is fully awake.
// Deliberately low: the settings preview's simulated pointer peaks near
// 324 px/s.
const float kFullSpeed = 200.0;

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
        float t_;
        best = min(best, pointerSegmentDistanceFrom(p, prev, v, t_));
        prev = v;
    }
    return best;
}

vec4 pPointer(vec2 uv) {
    // No early return on an empty trail: a click before any motion this
    // session still bursts (the burst answers a parked pointer by design),
    // and the trail block needs two live samples before it draws anything.
    int count = pointerTrailCount();

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    // The reach the host resolved from the `reach` parameter, in device px,
    // read from the uniform so the damage rect and the shader cannot drift.
    float reachPx = pointerReach();
    float rate = clamp(p_crackleRate, 1.0, 60.0);
    float jag = clamp(p_jaggedness, 0.0, 1.0);
    float intensity = max(p_intensity, 0.0);

    // Idle envelope: exactly zero once the pointer has been still for
    // kQuietSeconds, which is inside the metadata trailSeconds.
    float live = clamp(1.0 - pointerIdleSeconds() / kQuietSeconds, 0.0, 1.0);
    // Both from the filtered speed (see pointerFilteredSpeed): the raw
    // velocity is one event pair and reads 0 whenever two events share a
    // millisecond, which would blink the gate and the arc count.
    float speed = pointerFilteredSpeed();
    float activity = clamp(speed / (kFullSpeed * scale), 0.0, 1.0);
    float gate = pointerActivationGate(p_activationSpeed);

    float roll = floor(iTime * rate);
    float phase = fract(iTime * rate);
    // Sharp attack, straight decay: the arc strikes and dies inside its window.
    float strike = smoothstep(0.0, 0.12, phase) * (1.0 - phase);

    float core = 1.1 * scale;
    float glow = 5.5 * scale;
    // The budget an arc's excursion is fitted to (see REACH in the header).
    float strikeReach = max(reachPx - glow * 3.0, reachPx * 0.35);

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;

    // Number of lit arcs grows with speed; the last one fades in fractionally.
    float budget = 1.0 + (clamp(float(int(p_arcs + 0.5)), 1.0, float(kMaxArcs)) - 1.0) * activity;
    float arcAlpha = live * gate * strike * intensity * (0.35 + 0.65 * activity);
    // The newest-first index of the last sample still inside the quiet
    // window. Ages are monotonic in the index, so the first one at or past
    // the window ends the live run (see ENDPOINTS in the header).
    int last = 0;
    for (int i = 1; i < kPointerTrailCapacity && arcAlpha > 0.0; ++i) {
        if (i >= count || pointerTrailAt(i).z >= kQuietSeconds) {
            break;
        }
        last = i;
    }
    if (arcAlpha > 0.0 && last >= 1) {
        for (int j = 0; j < kMaxArcs; ++j) {
            if (float(j) >= budget) {
                break;
            }
            float share = clamp(budget - float(j), 0.0, 1.0);
            vec2 seed = vec2(roll * 1.731 + float(j) * 37.19, float(j) * 11.53 + 5.0);
            vec3 h = hash23(seed);

            int i0 = int(h.x * float(last) * 0.999);
            int span = 1 + int(h.y * 3.999);
            int i1 = min(i0 + span, last);
            vec2 a = pointerTrailAt(i0).xy;
            vec2 b = pointerTrailAt(i1).xy;
            vec2 d = b - a;
            float len = length(d);
            // The segment and its jag together must fit the budget.
            float lenMax = strikeReach / (1.0 + 0.22 * jag);
            if (len > lenMax) {
                b = a + d * (lenMax / len);
                len = lenMax;
            }
            if (len < scale) {
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
        // The strike reaches out over the burst's life and dims as it goes,
        // fitted to the same budget as the trail arcs.
        float len = strikeReach * (0.35 + 0.65 * k);
        float burstAlpha = remain * remain * intensity;
        for (int j = 0; j < kMaxArcs; ++j) {
            if (j >= burst) {
                break;
            }
            vec3 h = hash23(vec2(float(j) * 21.7 + 3.0, floor(origin.x) * 0.13 + floor(origin.y) * 0.29));
            float angle = (float(j) + 0.4 * h.x) / float(burst) * TAU;
            vec2 tip = origin + vec2(cos(angle), sin(angle)) * len * (0.55 + 0.45 * h.y);
            // The jag pushes vertices sideways off the straight line, so the
            // arc's whole excursion is its length plus its amplitude. Both
            // together are held inside the reach, or the kinks of a full-length
            // strike would hang outside the damage rect.
            float tipLen = length(tip - origin);
            float amp = min(jag * 0.22 * tipLen, max(strikeReach - tipLen, 0.0));
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
