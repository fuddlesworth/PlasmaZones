// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Charge pointer shader — a stroke that banks the pointer's speed as a
// charge and spends it as a pulse when the hand stops or a button is
// pressed. Sibling of Phosphor Trail, with speed as an event rather than a
// fade multiplier.
//
// THE IDEA. Speed is a BUDGET the stroke banks and then spends, not a fade
// multiplier. Sustained movement charges the stroke: it widens, whitens, and
// climbs the brand spectrum from cyan toward rose. Stopping spends the
// charge as a bright pulse that travels the length of the stroke from the
// pointer back to the tail, and leaves a thin quiet line behind it. A click
// spends it the same way without waiting for the hand to stop.
//
// That gives the pack an event with a beginning and an end, which is what
// every good pack in this family has and a plain fading tube does not.
//
// CHARGE IS DERIVED, NOT STORED. A fragment shader has no state between
// frames, so the charge is read back out of the history every frame: the
// mean of the per-sample speeds over the live run, against a fixed sample
// budget. Using a fixed budget rather than the live count is what makes a
// long stroke charge more than a flick of the same speed, since a flick
// fills only a few slots. This is the one legitimate use of the per-sample
// `.w` speeds outside something drawn AT a sample: it is a mean over many of
// them, so the per-sample jitter the filtered speed exists to remove cancels
// out instead of chattering.
//
// THE PULSE runs in the AGE coordinate, not in arc length. Age already
// increases monotonically from the pointer to the tail and every fragment
// has it, where arc length would need a prefix sum over the whole path per
// fragment. A band in age is a band along the stroke.
//
// TWO CLOCKS. A click discharges on pointerSincePress(), a stop on
// pointerIdleSeconds(). Both are bounded by kDischargeSeconds, which is well
// inside the metadata trailSeconds, so nothing is left lit when the host
// stops asking for frames. The click wins where both are running: it is the
// deliberate one.
//
// REACH. The bloom is cut off at four sigma and clamped to the reach, and
// the swell is inside the width the sigma is derived from, so a fully
// charged stroke does not grow past the damage rect.

const float kDischargeSeconds = 0.45;
// Sample budget the charge is measured against (see CHARGE IS DERIVED).
const float kChargeSamples = 14.0;

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 2) {
        return vec4(0.0);
    }

    float gate = pointerActivationGate(p_activationSpeed);
    if (gate <= 0.0) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float lifetime = max(p_lifetime, 0.05);

    // Only the LIVE run is drawn, smoothed over, or measured for charge.
    int live = pointerLiveCount(count, lifetime);
    if (live < 2) {
        return vec4(0.0);
    }

    // ── charge ──
    float chargeSpeed = max(p_chargeSpeed, 1.0) * scale;
    float sum = 0.0;
    for (int i = 0; i < kPointerTrailCapacity; ++i) {
        if (i >= live) {
            break;
        }
        // Each sample contributes at most its full share, so one hitched
        // frame with an absurd instantaneous speed cannot carry the charge on
        // its own.
        sum += min(pointerTrailAt(i).w / chargeSpeed, 1.0);
    }
    float charge = clamp(sum / kChargeSamples, 0.0, 1.0);

    // ── discharge ──
    // The click clock wins over the stop clock where both are inside the
    // window: a click is deliberate and a stop is not.
    float clock = -1.0;
    float sincePress = pointerSincePress();
    if (uPointerPress.w > 0.5 && sincePress < kDischargeSeconds) {
        clock = sincePress;
    } else {
        float idle = pointerIdleSeconds();
        if (idle < kDischargeSeconds) {
            clock = idle;
        }
    }
    float pulsePos = clock >= 0.0 ? clock / kDischargeSeconds : -1.0;
    // The pulse spends what was banked, so it is as bright as the charge was,
    // and it dies out as it reaches the tail.
    float pulseGain = pulsePos >= 0.0 ? max(p_discharge, 0.0) * charge * (1.0 - pulsePos) : 0.0;
    // Width of the band in the age coordinate. Wide enough to read as a
    // travelling swell rather than a hairline at any lifetime.
    const float kBand = 0.13;

    // A discharged stroke is a quiet one: while the pulse runs, the charge
    // the stroke still shows falls off behind the band.
    float held = pulsePos >= 0.0 ? charge * (1.0 - clamp(pulsePos * 1.4, 0.0, 1.0)) : charge;

    float halfWidth = 0.5 * max(p_width, 0.5) * scale * mix(1.0, max(p_swell, 1.0), held);
    float sigma = halfWidth * 2.0 + 1.5 * scale;
    float innerSigma = sigma * 0.5;
    float limit = min(sigma * 4.0, pointerReach());
    // Edge softness in DEVICE px, the family convention (scaling it makes the
    // stroke's edge twice as soft on a 2x display as every sibling pack's).
    // It grows a little with pointer speed: a stroke redrawn at discrete
    // frames while the hand sweeps genuinely has a soft edge, and a 1.5 px
    // hard edge on a stroke crossing 40 px between frames reads as a cut-out
    // ribbon rather than as light. The speed it answers to is the FILTERED
    // one, so the edge does not chatter between frames. Small enough that the
    // stroke is still crisp at rest.
    float feather = 0.75 + 1.6 * smoothstep(0.0, 1200.0 * scale, pointerFilteredSpeed());
    float hotSigma = max(halfWidth * 0.4, 0.6 * scale);

    float core = 0.0;
    float hot = 0.0;
    float halo = 0.0;
    float pulse = 0.0;
    float bestCover = 0.0;
    float bestAge = 0.0;

    // Four-point window over the smoothed path. The span drawn this
    // iteration is c1..c2 and c0 / c3 set its tangents; the window shifts by
    // one per iteration, so each sample is smoothed once rather than four
    // times. The newest end passes its endpoint twice, which gives that end a
    // zero tangent and a span that leaves it straight down the chord.
    vec2 c0 = pointerSmoothedAt(0, live, p_smoothing);
    vec2 c1 = c0;
    vec2 c2 = pointerSmoothedAt(1, live, p_smoothing);
    vec2 c3 = pointerSmoothedAt(2, live, p_smoothing);
    for (int i = 0; i < kPointerTrailCapacity - 1; ++i) {
        if (i + 1 >= live) {
            break;
        }
        vec4 a = pointerTrailAt(i);
        vec4 b = pointerTrailAt(i + 1);
        // The curve can leave the box of its control points, so the box the
        // cull is done on has to allow for it or a fragment the stroke
        // genuinely covers is skipped and the stroke is clipped.
        if (pointerSegmentOutside(px, i, live, a, b, limit + pointerCurveBulge(c0, c1, c2, c3))) {
            c0 = c1;
            c1 = c2;
            c2 = c3;
            c3 = pointerSmoothedAt(i + 3, live, p_smoothing);
            continue;
        }
        if (distance(a.xy, b.xy) < 1.0) {
            // A stationary pair has no tube to draw; drawing one would hold a
            // dot under a parked pointer that the compositor never ages out.
            c0 = c1;
            c1 = c2;
            c2 = c3;
            c3 = pointerSmoothedAt(i + 3, live, p_smoothing);
            continue;
        }
        float t;
        float d = pointerCurveDistanceFrom(px, c0, c1, c2, c3, t);
        c0 = c1;
            c1 = c2;
            c2 = c3;
            c3 = pointerSmoothedAt(i + 3, live, p_smoothing);
        if (d > limit) {
            continue;
        }
        float age = clamp(mix(a.z, b.z, t) / lifetime, 0.0, 1.0);
        float u = 1.0 - age;
        float fade = u * u;
        float c = (1.0 - smoothstep(halfWidth - feather, halfWidth + feather, d)) * fade;
        float ht = exp(-(d * d) / (2.0 * hotSigma * hotSigma)) * fade;
        float h = (0.62 * exp(-(d * d) / (2.0 * innerSigma * innerSigma))
                   + 0.38 * exp(-(d * d) / (2.0 * sigma * sigma)))
                  * fade * 0.45 * max(p_glow, 0.0);
        float thisCover = max(c, h);
        if (thisCover > bestCover) {
            bestCover = thisCover;
            bestAge = age;
        }
        if (pulseGain > 0.0) {
            float da = age - pulsePos;
            // The band is carried by the stroke, so it can only light what the
            // stroke already covers. That keeps it inside the reach and stops
            // it painting a bar across empty canvas.
            pulse = max(pulse, exp(-(da * da) / (2.0 * kBand * kBand)) * max(c, h * 1.6));
        }
        core = max(core, c);
        hot = max(hot, ht);
        halo = max(halo, h);
    }

    float cover = max(core, halo);
    if (cover <= 0.0) {
        return vec4(0.0);
    }

    // Colour climbs the spectrum with the charge the stroke is still holding,
    // with a small drift along the length so a long stroke is not one flat
    // colour. Clamped, never wrapped: the brand ramp has no return leg and a
    // wrap puts rose against cyan.
    vec3 rgb = phosphorGradient(clamp(held * 0.82 + 0.12 * bestAge, 0.0, 1.0));
    float burst = pulse * pulseGain;
    rgb = mix(rgb, vec3(1.0), clamp(hot * (0.35 + 0.5 * held) + 0.75 * burst, 0.0, 1.0));

    float alpha = clamp(cover * (1.0 + 0.9 * burst), 0.0, 1.0) * gate;
    return premul(min(rgb * (1.0 + 1.1 * burst), vec3(1.0)), alpha);
}
