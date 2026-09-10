// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Arc pointer shader — short jagged lightning arcs that snap between pairs of
// points on the pointer's recent path. Every arc is a polyline of kSegments
// straight pieces between two trail samples, each interior vertex pushed off
// the straight line by a hashed amount, so the arc reads as a crooked spark
// rather than a curve.
//
// DETERMINISM: a trail arc's endpoints and its whole jag pattern come from a
// hash of the ROLL INDEX, floor(iTime * rate), and the arc index. Every
// frame inside one roll window therefore draws the identical arc and the
// pack crackles at `rate` re-rolls a second instead of strobing at the frame
// rate. A click arc's tip is fixed for the burst's life (hashed from the
// press point and the arc index); only its kinks re-roll.
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
// plus three glow sigmas of halo, is fitted to a budget inside the reach so
// nothing meets the damage rect's edge at a visible level at ordinary
// settings. The budget is floored at 35 percent of the reach, as Burst
// floors its travel: at the smallest reach the halo alone is wider than the
// reach, and without the floor every strike would collapse onto its origin.
// Under the floor the halo does meet the rect's edge: at reach 16 it is
// clipped there at about a tenth of its brightness.
//
// COST. The live-run walk and every arc's endpoint derivation depend only on
// uniforms yet run per fragment, before the per-arc reject box can cull
// (there is no earlier place to put them in a single fragment pass). At
// eight arcs that is eight hashes and a few dozen trail reads per fragment
// over the trail's damage rect, which for a fast sweep is a large share of
// the output; affordable at eight, so do not grow kMaxArcs.

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

// Shade one arc at distance `dist` from it: a hot white core inside a
// coloured halo, weighted by `strength`, accumulated premultiplied into
// rgb / alpha. Shared by the trail arcs and the click fan so the two cannot
// drift apart in look.
void arcShade(float dist, float strength, float core, float glow, inout vec3 rgb, inout float alpha) {
    float hot = exp(-(dist * dist) / (2.0 * core * core));
    float halo = exp(-(dist * dist) / (2.0 * glow * glow)) * 0.55;
    float cover = clamp(hot + halo, 0.0, 1.0) * strength;
    if (cover <= 0.0) {
        return;
    }
    vec3 tint = mix(p_color.rgb, vec3(1.0), hot * 0.85);
    float ca = cover * p_color.a;
    rgb += tint * ca;
    alpha += ca;
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
    // Nudged onto a divisor of the preview's iTime wrap (pointerWrapSafeRate)
    // so the strike envelope does not snap once per wrap there; the
    // compositor never wraps. Capped at 30 (the metadata maximum): the
    // envelope is sampled once per frame, and a rate at the display's
    // refresh (60 on a 60 Hz output) advances the phase a whole cycle per
    // frame, so every frame sampled the same phase and the arcs sat dim
    // instead of crackling.
    //
    // The cap is HALF of 60, which is the fastest re-roll a 60 Hz output can
    // actually resolve. It is deliberately not lowered to half of the 20 Hz
    // this family elsewhere designs down to: 10 re-rolls a second is not a
    // crackle, and the whole parameter would be spent buying correctness on
    // outputs almost nobody has. On a 20-30 Hz output the top of the range
    // aliases back toward the dim arcs described above, which is a graceful
    // degrade of one slider rather than a broken pack.
    float rate = pointerWrapSafeRate(clamp(p_crackleRate, 1.0, 30.0));
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
    // window (see ENDPOINTS in the header). pointerLiveCount walks the same
    // monotonic ages and answers the run LENGTH, so the last index is one
    // less; a run of 0 or 1 leaves `last` at 0 and draws nothing, which is
    // what the guard below already expects.
    // Behind the alpha test, as the loop this replaced was: arcAlpha is 0 on
    // every idle-faded, gated-shut or between-strikes frame, which is most of
    // them, and there is no earlier exit in this pack.
    int last = arcAlpha > 0.0 ? max(pointerLiveCount(count, kQuietSeconds) - 1, 0) : 0;
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

            arcShade(arcDistance(px, a, b, seed, amp), arcAlpha * share * (0.6 + 0.4 * h.z), core, glow, rgb, alpha);
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
            arcShade(arcDistance(px, origin, tip, seed, amp), burstAlpha * (0.6 + 0.4 * h.z), core, glow, rgb, alpha);
        }
    }

    return premulAccumulated(rgb, alpha);
}
