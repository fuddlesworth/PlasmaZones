// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Trail pointer shader — one smooth tube of light along the recent
// path. A thin bright core sits inside a soft bloom, so the stroke reads as
// light rather than as paint, and it holds the same width from end to end.
// Only the brightness falls with age, which is what keeps its silhouette
// clear of the comet, whose tail narrows to a point behind a bright head.
//
// BRIGHTNESS PROFILE. Three terms, not two. `core` is the plateau that
// carries the constant-width silhouette and the alpha. `hot` is a narrow
// gaussian at its centre and is the only thing that whitens, so the filament
// goes white while the core's own edge stays fully coloured. It is the only
// thing that whitens the RESTING stroke; the click flare whitens too, on top
// of it. `halo` is the
// bloom, two gaussians sharing one peak (a tight lobe at half the width plus
// the wide one) rather than a single wide gaussian that spends its energy in
// the skirt. The core also gives up its brightness faster than the bloom, so
// an ageing stroke turns from filament into bare glow without ever changing
// width.
//
// Colour is read over TIME, not across the width. At any instant the whole
// tube is essentially one colour taken from the brand spectrum, and that
// colour walks along the spectrum once every `colorCycle` seconds. A very
// small offset is added along the length of the stroke so the tube has some
// depth, small enough that it never separates into bands.
//
// Coverage is the maximum over path segments, so a folded path does not stack
// into a brighter blob where it crosses itself.
//
// A press sends a bright pulse running outward from the press point. It only
// lights fragments the tube already covers, so it stays inside the declared
// reach and reads as the stroke itself lighting up.
//
// `activationSpeed` gates the whole tube once through the shared
// pointerActivationGate(), and `smoothing` goes through pointerSmoothedAt(),
// so this pack and every other speed-gated pack gate on the same filtered
// figure -- naming them here only rots as packs land -- and every path pack traces
// the same curve. Both default to 0, which is the no-threshold, raw-path
// behaviour.

const float kFlareSeconds = 0.4;
// The metadata trailSeconds. `lifetime` is this pack's trailWindowParam, so the
// host spaces the ring over it and inflates the damage rect for it; a value
// past this would walk samples the host has already stopped repainting.
const float kTrailSeconds = 2.0;

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 2) {
        return vec4(0.0);
    }

    // One gate for the whole tube, from the filtered speed: the raw per-sample
    // figure is one event pair and reads 0 whenever two events share a
    // millisecond, which gated per segment would blink patches of the tube.
    float gate = pointerActivationGate(p_activationSpeed);
    if (gate <= 0.0) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    // Floored against an UNSET uniform reading 0 rather than against a user
    // value (the metadata min is 0.2, so no setting reaches the floor), and
    // capped at the window the host actually spaced the ring over.
    float lifetime = clamp(p_lifetime, 0.05, kTrailSeconds);
    float halfWidth = 0.5 * max(p_width, 0.5) * scale;
    float sigma = halfWidth * 2.2 + 1.5 * scale;
    // The hot centre of the core. Narrow enough that the core's own edge stays
    // fully coloured, which is what reads as a filament of light inside a
    // coloured tube rather than a flat white ribbon with a coloured rim.
    // Floored so a 1 px stroke still has a centre to whiten.
    float hotSigma = max(halfWidth * 0.42, 0.6 * scale);
    // The bloom is two lobes sharing one peak, not one gaussian. A single
    // gaussian at `sigma` spends most of its energy in the wide skirt and the
    // stroke reads as a soft smudge; splitting it into a tight lobe at half
    // the width plus the wide one keeps the same peak and the same outer
    // limit while putting more of the light close to the core, which is the
    // contrast that makes it look bright.
    float innerSigma = sigma * 0.5;
    // Four sigma, where the bloom is under a thousandth, rather than three,
    // where it was still a visible 1.5% and cut off square. Bounded by the
    // reach too, since four sigma at the widest stroke is past it.
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

    // Press pulse, gone well inside trailSeconds.
    float flare = 0.0;
    float sincePress = pointerSincePress();
    if (p_clickFlare > 0.0 && uPointerPress.w > 0.5 && sincePress < kFlareSeconds) {
        float ct = sincePress / kFlareSeconds;
        float pulse = 900.0 * scale * sincePress;
        float band = 40.0 * scale;
        float dp = abs(length(px - uPointerPress.xy) - pulse);
        float decay = (1.0 - ct) * (1.0 - ct);
        flare = exp(-(dp * dp) / (2.0 * band * band)) * decay * p_clickFlare;
    }

    // Colour walk. A cycle of 0 holds the tube on one colour forever.
    //
    // Ping-pong rather than wrap: the brand gradient runs cyan to rose with no
    // return leg, so a plain fract() would take the whole tube from rose back
    // to cyan in a single frame once per cycle. Walking back down the ramp
    // keeps it the continuous drift the pack advertises, at the cost of a
    // period twice the setting.
    //
    // The walk rate is nudged onto a divisor of the iTime wrap
    // (pointerWrapSafeRate) so the colour does not jump when iTime wraps in
    // the preview. On the compositor iTime restarts at 0 for every burst of
    // pointer activity instead, so the quarter-cycle offset starts each burst
    // in the middle of the ramp (blue to purple) rather than always at the
    // rose end, and ordinary use sees the whole spectrum rather than mostly
    // its top.
    float cycle = max(p_colorCycle, 0.0);
    float hueBase =
        cycle > 0.0 ? abs(fract(iTime * pointerWrapSafeRate(1.0 / (cycle * 2.0)) + 0.25) * 2.0 - 1.0) : 0.35;

    float core = 0.0;
    float hot = 0.0;
    float halo = 0.0;
    float hueAge = 0.0;
    float bestCover = 0.0;

    // Only the LIVE run of samples is drawn or smoothed over: at a lifetime
    // equal to the metadata trailSeconds the samples behind it are outside
    // the damage rect, and the smoothing kernel would otherwise blend a
    // segment's far end toward one of them. The live count is the window
    // pointerSmoothedAt clamps its neighbours into.
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
        // Reject on the raw-sample box (see pointerSegmentOutside) before the
        // distance maths.
        // The curve can leave the box of its control points, so the box the
        // cull is done on has to allow for it or a fragment the stroke
        // genuinely covers is skipped and the stroke is clipped.
        if (pointerSegmentOutside(px, i, live, a, b, limit + pointerCurveBulge(c0, c1, c2, c3))) {
            pointerCurveAdvance(i + 3, live, p_smoothing);
            continue;
        }
        if (distance(a.xy, b.xy) < 1.0) {
            // A stationary pair (raw positions under a pixel apart, the
            // sampler's own rest-slot rule) has no tube to draw. A host that
            // feeds a resting pointer appends one every interval,
            // and a point segment at age zero would hold a full-brightness
            // dot at the rest point where the compositor lets it age out.
            // Tested on the raw samples, since the smoothing kernel pulls the
            // pair's endpoints apart toward the neighbour beyond.
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
        // The core gives up its brightness faster than the bloom does, so the
        // stroke ages from a filament with a halo into bare glow. The width is
        // untouched, which is the pack's silhouette; only the mix of core and
        // bloom moves, and that is what gives the tube depth along its length.
        float fadeCore = fade * mix(1.0, 0.55, age);
        float c = (1.0 - smoothstep(halfWidth - feather, halfWidth + feather, d)) * fadeCore;
        float ht = exp(-(d * d) / (2.0 * hotSigma * hotSigma)) * fadeCore;
        float h = (0.62 * exp(-(d * d) / (2.0 * innerSigma * innerSigma))
                   + 0.38 * exp(-(d * d) / (2.0 * sigma * sigma)))
                  * fade * 0.45 * max(p_glow, 0.0);
        // Tracked against a running best of the same quantity `cover` is taken
        // from below. This is a clarity change, not a behaviour one: a max of
        // pairwise maxima equals the max of the separate maxima, so the old
        // `max(c, h) > max(core, halo)` test fired on exactly these segments.
        // The argmax form says what is meant, and costs one register.
        float thisCover = max(c, h);
        if (thisCover > bestCover) {
            bestCover = thisCover;
            hueAge = age;
        }
        core = max(core, c);
        hot = max(hot, ht);
        halo = max(halo, h);
    }

    float cover = max(core, halo);
    if (cover <= 0.0) {
        return vec4(0.0);
    }

    // The small along-the-length offset that gives the tube depth.
    // CLAMP, not fract. The along-the-length offset is what gives the tube its
    // depth, but wrapping it puts rose next to cyan wherever the walk is near
    // the top of the ramp — the same maximum-contrast seam the ping-pong above
    // removes, just running along the stroke instead of across the whole tube.
    // The base is scaled to leave room for the offset, so ACROSS THE STROKE
    // the walk still reaches both ends: the two coefficients sum to 1, so the
    // tail at a full base sits exactly at the top of the ramp. The head tops
    // out at 0.88 of it, which is the room the offset needed and is what gives
    // the tube its depth. Because they sum to 1 the clamp can never bind on
    // the current coefficients; it is kept as the guard for retuning them,
    // since a sum above 1 would wrap rose onto cyan rather than saturate.
    vec3 rgb = phosphorGradient(clamp(hueBase * 0.88 + 0.12 * hueAge, 0.0, 1.0));
    // The whitening is driven by the HOT term, not by the core's coverage.
    // Coverage is a plateau across the whole core width, so driving it from
    // there washed the entire core to the same pale tint and flattened it.
    // Taken from the narrow gaussian instead, only the centre line goes white
    // and the colour is still full strength at the core's edge.
    float whiten = hot * 0.85;
    rgb = mix(rgb, vec3(1.0), clamp(whiten + 0.6 * flare, 0.0, 1.0));

    float alpha = clamp(cover * (1.0 + 0.5 * flare), 0.0, 1.0) * gate;
    return premul(min(rgb * (1.0 + 1.2 * flare), vec3(1.0)), alpha);
}
