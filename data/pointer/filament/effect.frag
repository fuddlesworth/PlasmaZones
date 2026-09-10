// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Filament pointer shader — Phosphor Trail's tube with current running
// through it: bright nodes travelling toward the pointer, faster as the hand
// moves faster. Same silhouette, motion along the length instead of only a
// fade.
//
// THE IDEA. The tube keeps the constant width Phosphor Trail owns (the age
// fade is filament's own, and is shallower, because the train already gives
// the stroke motion) and gains motion ALONG ITS LENGTH: bright nodes travelling
// toward the pointer, faster as the hand moves faster. A stroke that carries
// current reads as live; a stroke that only dims reads as a smear. The
// silhouette is unchanged, so the pack still sits clearly apart from the
// comet and its narrowing tail.
//
// THE NODE COORDINATE is AGE, not arc length. Age increases monotonically
// from the pointer to the tail and every fragment already has it; arc length
// would need a prefix sum over the whole path per fragment for a difference
// nobody could see. Nodes move toward the pointer, so the phase is ADDED to
// the age: a node sits where age * nodes + phase is constant, and since the
// phase grows the node's age SHRINKS, which is the walk from the tail to the
// pointer.
//
// FLOW SPEED, AND WHY THE RATE IS CONSTANT. `flow` is a rate in cycles per
// second and nothing lifts it with the pointer's speed, because the racing is
// already there for free. A node holds a constant speed in AGE, and the live
// run always spans `lifetime` seconds of age however long it is in pixels, so
// a path of length speed * lifetime carries that node across the screen at a
// rate proportional to the pointer's own. Lifting the rate as well would be
// worse than redundant: `phase` is iTime * rate, not an integral of the rate,
// so any rate that moves per frame steps the whole train by iTime * dRate at
// once. That is a jump of whole cycles seconds into a drag, and it reads as
// scintillation rather than as acceleration. The rate goes through
// pointerWrapSafeRate so the phase does not jump when iTime wraps in the
// settings preview; on the compositor iTime restarts per burst of pointer
// activity and never wraps.
//
// The nodes modulate the CORE only. The bloom stays smooth, so while there is
// bloom to carry it the stroke reads as one continuous tube of light with
// something moving inside
// it rather than as a dotted line, and the alpha never drops out between
// nodes.
//
// A click fires a single packet: a bright band that starts at the pointer and
// runs to the tail inside kPacketSeconds, well within the metadata
// trailSeconds. It only lights what the tube already covers, so it stays
// inside the declared reach.

const float kPacketSeconds = 0.4;
// The metadata trailSeconds. `lifetime` is this pack's trailWindowParam, so
// the host spaces the ring over it; a value past this would walk samples the
// host has already dropped from the damage rect and freeze their last sliver.
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
    // one, so the edge does not chatter between frames. That figure does not
    // decay when the hand stops -- it is measured over the current stroke and
    // holds its last value -- so a stroke that swept fast keeps its softest
    // edge for the whole fade rather than crisping up as it dies.
    float feather = 0.75 + 1.6 * smoothstep(0.0, 1200.0 * scale, pointerFilteredSpeed());

    // The travelling phase, at a rate that does not move: see FLOW SPEED
    // above. A sweep races because the path it is painted along is longer,
    // not because this number grows.
    float flowRate = max(p_flow, 0.0);
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
            pointerCurveAdvance(i + 3, live, p_smoothing);
            continue;
        }
        if (distance(a.xy, b.xy) < 1.0) {
            // A stationary pair has no tube to draw; drawing one would hold a
            // dot under a parked pointer that the compositor never ages out.
            pointerCurveAdvance(i + 3, live, p_smoothing);
            continue;
        }
        float t;
        float d = pointerCurveDistanceFrom(px, c0, c1, c2, c3, t);
        pointerCurveAdvance(i + 3, live, p_smoothing);
        if (d > limit) {
            continue;
        }
        float age = clamp(mix(a.z, b.z, t) / lifetime, 0.0, 1.0);
        float u = 1.0 - age;
        float fade = u * u;

        // The node train. Adding the phase runs the nodes toward the pointer
        // (a node holds age * nodes + phase constant, so a growing phase walks
        // it to a smaller age), which is the direction that reads as current
        // arriving rather than as the trail draining away.
        float wave = 0.5 + 0.5 * sin((age * nodes + phase) * TAU);
        // Sharpened so the nodes are beads rather than a soft ripple, then
        // mixed in by `depth` so 0 leaves the even tube.
        float train = mix(1.0, wave * wave, depth);

        // The core silhouette before the train chops it. The packet is
        // sampled off this rather than off `c`, because the packet runs from
        // the pointer to the tail while the train runs the other way, and a
        // band read through a counter-propagating chopper is strobed into
        // fragments instead of arriving as one bright pulse.
        float cFlat = (1.0 - smoothstep(halfWidth - feather, halfWidth + feather, d)) * fade;
        float c = cFlat * train;
        float ht = exp(-(d * d) / (2.0 * hotSigma * hotSigma)) * fade * train;
        // The bloom is NOT modulated by the train: while there is bloom, the
        // tube stays continuous and only what is inside it moves. At glow 0
        // there is none, so the train chops the core alone and a deep enough
        // `depth` really does read as a dotted line.
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
            packet = max(packet, exp(-(da * da) / (2.0 * kPacketBand * kPacketBand)) * max(cFlat, h * 1.6));
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
    // the ramp. The two coefficients sum to 1, so across the stroke the walk
    // reaches both ends and the head tops out at 0.88 of the ramp, which is
    // the room the offset needed. That also means the clamp cannot bind on
    // these coefficients; it is the guard for retuning them, since a sum above
    // 1 would wrap rose onto cyan rather than saturate. Kept in step with
    // phosphor-trail, which shares this walk.
    vec3 rgb = phosphorGradient(clamp(hueBase * 0.88 + 0.12 * bestAge, 0.0, 1.0));
    float burst = packet * packetGain;
    rgb = mix(rgb, vec3(1.0), clamp(hot * 0.85 + 0.7 * burst, 0.0, 1.0));

    float alpha = clamp(cover * (1.0 + 0.7 * burst), 0.0, 1.0) * gate;
    return premul(min(rgb * (1.0 + 1.0 * burst), vec3(1.0)), alpha);
}
