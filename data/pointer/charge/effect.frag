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
// frames, so the charge is read back out of the history every frame: the mean
// of the per-sample speeds over the live run, scaled by how long that run has
// been going. The second factor is what makes a long stroke charge more than
// a flick of the same speed. It is measured in TIME, not in slots, because
// the number of slots is not this pack's to know: the host spaces the one
// shared ring over the LONGEST trail window in the whole chain, so the same
// stroke fills ~32 slots with this pack alone and ~14 beside a long-lifetime
// pack. A slot budget would hand a slider in an unrelated pack the power to
// retune this one's headline mechanic. This is the one legitimate use of the per-sample
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
// pointerIdleSeconds() once it is past kSettleSeconds. Both bands are bounded
// by kDischargeSeconds, which is well inside the metadata trailSeconds, so
// nothing is left lit when the host stops asking for frames. The click wins
// where both are running: it is the deliberate one.
//
// THE SETTLE GATE IS NOT OPTIONAL. pointerIdleSeconds() is seconds since the
// last accepted motion, which during a drag is one frame delta. Without the
// gate the stop branch is taken on every frame of every movement, the band
// sits at age zero welded under the cursor, and the pack has no event at all.
//
// It narrows rather than closes. Accepted events need one device px of
// travel, so a crawl under about ten px per second lands one less often than
// kSettleSeconds and `idle` crosses the gate each time, starting a discharge
// that the next event aborts. No threshold closes that: raising it only moves
// the flicker to a slower crawl, and latching the pulse needs state a
// fragment shader does not have. The filtered speed is NOT the way out --
// it deliberately holds its value across a rest rather than decaying
// (PointerHistory::filteredSpeed skips non-motion samples), so gating on it
// would stop the discharge ever firing after a real stroke. Left as it is
// because the host already agrees: at that crawl its own sampler scores the
// samples at speed zero and marks a stroke start, so it considers the hand
// parked too.
//
// WHAT IS SPENT STAYS SPENT. `spent` is tracked separately from the band's
// position and does not reset when the band reaches the tail, or the stroke
// would snap from thin-and-quiet back to fully swelled in one frame at the
// end of every discharge -- the charge is re-derived from a ring that still
// holds the fast samples. After a stop it stays spent for as long as the
// pointer is parked; after a click it eases back over kRecoverSeconds,
// because the hand is still moving and genuinely still charging.
//
// THE ONE STEP THAT REMAINS, and why. Resuming from a stop takes `spent` back
// to 0 in a frame, so the stroke re-energises at once instead of building.
// The bank behind it is genuinely stale: the ring does NOT drain while the
// pointer is parked, because on the compositor notePointer() is driven by
// mouseChanged and a still pointer sends nothing at all, so the fast samples
// sit there until they age out of `lifetime`. Easing this out the way the
// click arm does needs a clock that keeps running after a stop, and the
// contract carries no lane that yields time-since-resume the same way on both
// runtimes. pointerIdleSeconds() resets on the first accepted motion, which
// is the very moment in question; iTime survives the park but marks nothing
// at the resume; and the gap in the trail ages that a park leaves exists on
// the compositor but not in the settings preview, whose sampler keeps
// inserting evenly spaced rest slots. The step is bounded: it can only appear
// for a park shorter than `lifetime`, and a park past that empties the live
// run and starts the stroke from nothing anyway.
// Do not paper over it by decaying the charge with idle time; that reads as
// the stroke dimming while it is parked, which is the discharge's job.
//
// REACH. The bloom is cut off at four sigma and clamped to the reach, and
// the swell is inside the width the sigma is derived from, so a fully
// charged stroke does not grow past the damage rect.

const float kDischargeSeconds = 0.45;
// How long a stroke has to have been running to bank a full charge (seconds).
const float kChargeSeconds = 0.35;
// How long the pointer must be still before a STOP counts as a stop. This is
// the host's own kVelocityHoldMs: below it the sampler still considers the
// hand to be inside one continuous stroke, above it the sampler has already
// declared the stroke over and scores the samples at speed zero. Using the
// same number means the pack and the host agree on what a stop is, and it is
// far enough above the ~59 ms a 17 px/s inspection crawl takes to move one
// device px that a slow, deliberate drag does not keep firing the discharge.
const float kSettleSeconds = 0.10;
// How long the stroke takes to climb back to its banked charge after a click
// discharge, while the hand is still moving.
const float kRecoverSeconds = 0.35;

// The metadata trailSeconds. `lifetime` is this pack's trailWindowParam, so the
// host spaces the ring over it and inflates the damage rect for it; a value
// past this would walk samples the host has already stopped repainting.
const float kTrailSeconds = 2.0;

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
    float lifetime = clamp(p_lifetime, 0.05, kTrailSeconds);

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
    // Mean over the run, then scaled by how long the run has actually been
    // going. Both factors are independent of how the host spaced the ring.
    float mean = sum / float(live);
    float runSeconds = pointerTrailAt(live - 1).z;
    float charge = clamp(mean * clamp(runSeconds / kChargeSeconds, 0.0, 1.0), 0.0, 1.0);

    // ── discharge ──
    // The click clock wins over the stop clock where both are inside the
    // window: a click is deliberate and a stop is not.
    float clock = -1.0;
    float spent = 0.0;
    float sincePress = pointerSincePress();
    float idle = pointerIdleSeconds();
    if (uPointerPress.w > 0.5 && sincePress < kDischargeSeconds + kRecoverSeconds) {
        if (sincePress < kDischargeSeconds) {
            clock = sincePress;
            spent = clamp(sincePress * 1.4 / kDischargeSeconds, 0.0, 1.0);
        } else {
            spent = 1.0 - clamp((sincePress - kDischargeSeconds) / kRecoverSeconds, 0.0, 1.0);
        }
    } else if (idle >= kSettleSeconds) {
        float stopped = idle - kSettleSeconds;
        if (stopped < kDischargeSeconds) {
            clock = stopped;
        }
        spent = clamp(stopped * 1.4 / kDischargeSeconds, 0.0, 1.0);
    }
    float pulsePos = clock >= 0.0 ? clock / kDischargeSeconds : -1.0;
    // The pulse spends what was banked, so it is as bright as the charge was,
    // and it dies out as it reaches the tail.
    float pulseGain = pulsePos >= 0.0 ? max(p_discharge, 0.0) * charge * (1.0 - pulsePos) : 0.0;
    // Width of the band in the age coordinate. Wide enough to read as a
    // travelling swell rather than a hairline at any lifetime.
    const float kBand = 0.13;

    // A discharged stroke is a quiet one, and it stays quiet: see WHAT IS
    // SPENT STAYS SPENT above.
    float held = charge * (1.0 - spent);

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
    // one, so the edge does not chatter between frames. That figure does not
    // decay when the hand stops -- it is measured over the current stroke and
    // holds its last value -- so a stroke that swept fast keeps its softest
    // edge for the whole fade rather than crisping up as it dies.
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
    // times. The newest end passes its endpoint twice, so that end's
    // tangent is the chord itself and the span leaves it straight.
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
        // Windowed against the reach, not merely clamped by `limit`. The
        // swell multiplies the half-width the sigma is derived from, so at a
        // wide, fully charged stroke four sigma is several times the reach and
        // the bloom is still worth ~30% alpha where `limit` cuts it -- a hard
        // square edge at the damage rect. The window takes it to zero there
        // instead, which is what the sibling packs that can outgrow their
        // reach already do.
        float h = (0.62 * exp(-(d * d) / (2.0 * innerSigma * innerSigma))
                   + 0.38 * exp(-(d * d) / (2.0 * sigma * sigma)))
                  * pointerReachWindow(d, limit)
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
