// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Filament pointer shader — Phosphor Trail's tube with current running
// through it: bright nodes travelling toward the pointer, faster as the hand
// moves faster. Same silhouette, motion along the length instead of only a
// fade.
//
// THE IDEA. The tube keeps the constant width and the age fade Phosphor
// Trail owns, and gains motion ALONG ITS LENGTH: bright nodes travelling
// toward the pointer, faster as the hand moves faster. A stroke that carries
// current reads as live; a stroke that only dims reads as a smear. The
// silhouette is unchanged, so the pack still sits clearly apart from the
// comet and its narrowing tail.
//
// THE NODE COORDINATE is AGE, not arc length. Age increases monotonically
// from the pointer to the tail and every fragment already has it; arc length
// would need a prefix sum over the whole path per fragment for a difference
// nobody could see. Nodes move toward the pointer, so the phase is
// SUBTRACTED from the age.
//
// FLOW SPEED. The travel rate is the `flow` parameter lifted by the pointer's
// FILTERED speed, not its raw per-sample speed: the per-sample figure reads 0
// whenever two events share a millisecond, and the nodes would stutter on a
// steady drag. It goes through pointerWrapSafeRate so the phase does not jump
// when iTime wraps in the settings preview; on the compositor iTime restarts
// per burst of pointer activity and never wraps.
//
// The nodes modulate the CORE only. The bloom stays smooth, so the stroke
// still reads as one continuous tube of light with something moving inside
// it rather than as a dotted line, and the alpha never drops out between
// nodes.
//
// A click fires a single packet: a bright band that starts at the pointer and
// runs to the tail inside kPacketSeconds, well within the metadata
// trailSeconds. It only lights what the tube already covers, so it stays
// inside the declared reach.

const float kPacketSeconds = 0.4;
// Speed at which the flow is fully lifted (logical px/s, scaled at use).
const float kFullSpeed = 350.0;

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
    float halfWidth = 0.5 * max(p_width, 0.5) * scale;
    float sigma = halfWidth * 2.2 + 1.5 * scale;
    float innerSigma = sigma * 0.5;
    float hotSigma = max(halfWidth * 0.42, 0.6 * scale);
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

    // The travelling phase. The flow parameter is the resting rate in cycles
    // per second and the pointer's speed lifts it up to three times that, so
    // a fast sweep visibly races.
    float activity = clamp(pointerFilteredSpeed() / (kFullSpeed * scale), 0.0, 1.0);
    float flowRate = max(p_flow, 0.0) * (1.0 + 2.0 * activity);
    float phase = flowRate > 0.0 ? iTime * pointerWrapSafeRate(flowRate) : 0.0;
    float nodes = max(p_nodes, 0.5);
    float depth = clamp(p_depth, 0.0, 1.0);

    // Click packet: a band running from the pointer to the tail.
    float packetPos = -1.0;
    float packetGain = 0.0;
    float sincePress = pointerSincePress();
    if (p_clickPacket > 0.0 && uPointerPress.w > 0.5 && sincePress < kPacketSeconds) {
        packetPos = sincePress / kPacketSeconds;
        packetGain = max(p_clickPacket, 0.0) * (1.0 - packetPos);
    }
    const float kPacketBand = 0.11;

    // Colour walk. Ping-pong rather than wrap: the brand gradient runs cyan to
    // rose with no return leg, so a plain fract() would take the whole tube
    // from rose back to cyan in a single frame once per cycle.
    float cycle = max(p_colorCycle, 0.0);
    float hueBase =
        cycle > 0.0 ? abs(fract(iTime * pointerWrapSafeRate(1.0 / (cycle * 2.0)) + 0.25) * 2.0 - 1.0) : 0.35;

    float core = 0.0;
    float hot = 0.0;
    float halo = 0.0;
    float packet = 0.0;
    float bestCover = 0.0;
    float bestAge = 0.0;

    int live = pointerLiveCount(count, lifetime);
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

        // The node train. Subtracting the phase runs the nodes toward the
        // pointer, which is the direction that reads as current arriving
        // rather than as the trail draining away.
        float wave = 0.5 + 0.5 * sin((age * nodes - phase) * TAU);
        // Sharpened so the nodes are beads rather than a soft ripple, then
        // mixed in by `depth` so 0 leaves the even tube.
        float train = mix(1.0, wave * wave, depth);

        float c = (1.0 - smoothstep(halfWidth - feather, halfWidth + feather, d)) * fade * train;
        float ht = exp(-(d * d) / (2.0 * hotSigma * hotSigma)) * fade * train;
        // The bloom is NOT modulated by the train: the tube stays continuous
        // and only what is inside it moves.
        float h = (0.62 * exp(-(d * d) / (2.0 * innerSigma * innerSigma))
                   + 0.38 * exp(-(d * d) / (2.0 * sigma * sigma)))
                  * fade * 0.45 * max(p_glow, 0.0);
        float thisCover = max(c, h);
        if (thisCover > bestCover) {
            bestCover = thisCover;
            bestAge = age;
        }
        if (packetGain > 0.0) {
            float da = age - packetPos;
            packet = max(packet, exp(-(da * da) / (2.0 * kPacketBand * kPacketBand)) * max(c, h * 1.6));
        }
        core = max(core, c);
        hot = max(hot, ht);
        halo = max(halo, h);
    }

    float cover = max(core, halo);
    if (cover <= 0.0) {
        return vec4(0.0);
    }

    // A small offset along the length gives the tube depth. CLAMP, not fract:
    // wrapping puts rose next to cyan wherever the walk is near the top of
    // the ramp.
    vec3 rgb = phosphorGradient(clamp(hueBase * 0.88 + 0.12 * bestAge, 0.0, 1.0));
    float burst = packet * packetGain;
    rgb = mix(rgb, vec3(1.0), clamp(hot * 0.85 + 0.7 * burst, 0.0, 1.0));

    float alpha = clamp(cover * (1.0 + 0.7 * burst), 0.0, 1.0) * gate;
    return premul(min(rgb * (1.0 + 1.0 * burst), vec3(1.0)), alpha);
}
