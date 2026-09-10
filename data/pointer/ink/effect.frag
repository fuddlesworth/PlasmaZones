// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ink pointer shader — a calligraphic brush stroke. The one pack in this
// family that is not made of light: it returns an opaque matte colour with no
// bloom and no additive brightening, so it sits on the desktop like a mark on
// paper rather than glowing over it.
//
// The stroke follows the shared Catmull-Rom curve through the smoothed
// samples, like every other path pack: a brush mark with visible facets in it
// would give the whole conceit away faster than a glowing one would.
//
// NO ACTIVATION GATE, alone among the Trail-category path packs. `ink` is a
// mark rather than a light, and a mark that only appears above a speed
// threshold is not a mark. The omission is deliberate; it is not a parameter
// that was forgotten.
//
// Width answers to speed INVERSELY, which is what makes it read as a brush.
// Every other pack here gets wider the faster you move. A real brush or pen
// does the opposite: press slowly and it spreads, whip it across the page and
// it thins to a hairline. That inversion is the whole identity of the pack.
//
// An earlier revision broke the edge up with two octaves of value noise to
// suggest bristles. It read as grain and dirt rather than as a brush, so the
// noise is gone entirely. The edge is a clean antialiased band and the
// character comes from the width curve and the drying instead.
//
// Drying is age-asymmetric and deliberate: a stroke clears from its OLDEST end
// forward, so the tail lifts off the page first and the ink at the cursor is
// the last to go. Nothing else in the family fades that way round.
//
// `smoothing` goes through the shared pointerSmoothedAt(), like every other
// path pack, so two packs in one chain trace the same curve from the same
// pointer. It defaults to 0.5, the value the pack used to hardcode.

// Speed at which the stroke reaches its thinnest, in logical px/s (scaled at
// use).
const float kThinSpeed = 1200.0;
// The metadata trailSeconds. The host does not clamp parameters to their
// declared range, so a hand-edited lifetime past this would outlive the
// window and freeze its last frame on screen.
const float kTrailSeconds = 2.5;

// Wet ink spreads a little into the paper for the first part of its life,
// then stops. It never spreads again once dry. Shared by the stroke and the
// blot so the two swell on one curve.
float inkWet(float remain) {
    return 1.0 - smoothstep(0.0, 0.35, 1.0 - remain);
}

// Drying: held at full opacity for most of the life, then taken off quickly,
// which is what makes the tail vanish rather than dim evenly. Shared by the
// stroke and the blot so the two dry on one curve.
float inkDry(float remain) {
    return smoothstep(0.0, 0.30, remain);
}

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float halfWidth = 0.5 * max(p_width, 1.0) * scale;
    float lifetime = clamp(p_lifetime, 0.05, kTrailSeconds);
    float bleed = clamp(p_bleed, 0.0, 1.0);

    float cover = 0.0;

    // ── The stroke ──
    // Only the LIVE run of samples is drawn or smoothed over. The ring is
    // never purged, so behind the live run sit samples older than the
    // lifetime, and at a lifetime equal to the metadata trailSeconds those
    // are outside the damage rect, which covers only live samples. A segment
    // reaching one would leave its last sliver frozen there on the
    // compositor, and the smoothing kernel blends a segment's far end toward
    // the sample beyond it, so the live count is handed to pointerSmoothedAt
    // as the window it clamps its neighbours into.
    int live = pointerLiveCount(count, lifetime);
    if (live >= 2) {
        float cull = halfWidth * 1.5 + 2.0 * scale;
        // Four-point window over the smoothed path. The span drawn this
        // iteration is c1..c2 and c0 / c3 set its tangents; the window shifts
        // by one per iteration, so each sample is smoothed once rather than
        // four times. The newest end passes its endpoint twice, so that
        // end's tangent is the chord itself and the span leaves it straight.
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

            if (distance(a.xy, b.xy) < 1.0) {
                // A stationary pair (raw positions under a pixel apart, the
                // sampler's own rest-slot rule: a rest slot a host that feeds
                // every tick appends beside the last motion sample) lays down
                // no stroke. Tested on the raw samples,
                // since the smoothing kernel pulls the pair's endpoints
                // apart toward the neighbour beyond, and drawing that stub
                // would keep the rest point wet where the compositor, which
                // gets no event from a resting pointer, lets it dry.
                pointerCurveAdvance(i + 3, live, p_smoothing);
                continue;
            }
            // The box is over all four control points rather than the span's
            // two ends. That is deliberately CONSERVATIVE, not required: the
            // bulge already bounds the curve against the box of c1..c2 alone
            // (see pointerCurveBulge), and c0 / c3 are not on the curve at
            // all. Taking them in costs a slightly larger box and buys never
            // having to think about the bound again.
            float bulge = pointerCurveBulge(c0, c1, c2, c3);
            vec2 lo = min(min(c0, c1), min(c2, c3)) - cull - bulge;
            vec2 hi = max(max(c0, c1), max(c2, c3)) + cull + bulge;
            if (px.x < lo.x || px.y < lo.y || px.x > hi.x || px.y > hi.y) {
                pointerCurveAdvance(i + 3, live, p_smoothing);
                continue;
            }

            float t;
            float d = pointerCurveDistanceFrom(px, c0, c1, c2, c3, t);
            pointerCurveAdvance(i + 3, live, p_smoothing);
            float age = mix(a.z, b.z, t);
            float remain = 1.0 - age / lifetime;

            // The brush curve: broad where the hand was slow, thin where it
            // was quick.
            float speedNorm = clamp(mix(a.w, b.w, t) / (kThinSpeed * scale), 0.0, 1.0);
            float w = halfWidth * mix(1.0, 0.30, sqrt(speedNorm));

            float wet = inkWet(remain);
            w *= 1.0 + 0.18 * bleed * wet;
            w = max(w, 0.4 * scale);

            // A clean antialiased edge. This is the only softness in the pack.
            float band = 1.0 - smoothstep(w - 0.75, w + 0.75, d);

            // Drying: the oldest end of the stroke lifts first.
            float dry = inkDry(remain);
            cover = max(cover, band * dry);
        }
    }

    // ── The blot ──
    // A click lands a drop of ink. Round and clean, on the same wet then dry
    // curve as the stroke. Bounded inside the reach `width` buys.
    float blot = clamp(p_blot, 0.0, 0.7);
    float since = pointerSincePress();
    if (blot > 0.0 && uPointerPress.w > 0.5 && since < lifetime) {
        float remain = 1.0 - since / lifetime;
        float wet = inkWet(remain);
        // Widest while WET and drawing back in as it dries, the same shape and
        // the same direction as the stroke's own width term. Gated on Bleed for
        // the same reason, so setting Bleed to zero really does keep one width
        // everywhere.
        // The feather is folded into the reach, so the blot's edge ends inside
        // the damage rect at the smallest width too.
        float r = min(halfWidth * blot * 2.0 * (1.0 + 0.18 * bleed * wet), pointerReach() - 0.75);
        float d = length(px - uPointerPress.xy);
        float band = 1.0 - smoothstep(r - 0.75, r + 0.75, d);
        cover = max(cover, band * inkDry(remain));
    }

    float alpha = clamp(cover * p_color.a, 0.0, 1.0);
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    return premul(p_color.rgb, alpha);
}
