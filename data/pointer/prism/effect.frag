// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Prism pointer shader — a stroke of light whose colours separate with
// speed. Sibling of Phosphor Trail: the same constant-width tube and age
// fade, with the spectrum read across the width instead of held flat.
//
// THE IDEA. Colour is read ACROSS THE WIDTH of the stroke, not along its
// length and not over time. One edge of the stroke is cyan, the other is
// rose, and the brand spectrum runs between them through a white filament in
// the middle. How far apart the colours sit is driven by pointer speed: as
// the hand slows the spectrum draws back together and the stroke goes white,
// and at speed it opens into the full ramp. It is the hand SLOWING that
// closes it, not the hand stopping: the filtered speed is measured over the
// current stroke and holds its last value once the pointer parks, so a stroke
// that swept fast keeps its split for the whole fade. That makes
// the palette the mechanic rather than a tint, which is what Phosphor Trail
// never had.
//
// The stroke's silhouette is a constant-width tube that dims with age, the
// same silhouette Phosphor Trail owns and the thing that keeps it clear of
// the comet. Only the colour behaviour is new.
//
// SIGNED OFFSET. The split needs to know WHICH SIDE of the stroke a fragment
// is on, so this pack computes the perpendicular offset itself rather than
// taking the unsigned distance from the shared helper: the closest point on
// the span comes back through `t`, and the sign is the cross product of the
// curve's TANGENT AT THAT POINT with the fragment's offset from it. Taking
// the chord direction and the near end instead would disagree with the drawn
// curve by the whole turn angle at a bend. The
// magnitude is still the same distance the helper returns, so the silhouette
// and the reject boxes are identical to every other path pack's.
//
// SIDE CONTINUITY. The side is taken from the WINNING segment only, the one
// that covers the fragment most. Blending sides across segments would flip
// the spectrum end for end wherever the path doubles back, and a max over
// segments already picks one answer per fragment for age, so this is the
// same rule applied to one more quantity.
//
// REACH is Phosphor Trail's: a fixed `reach` in the metadata, with the bloom
// cut off at four sigma and clamped to it, so nothing meets the damage
// rect's edge at a visible level.
//
// A press throws the dispersion wide for kFlareSeconds and brightens the
// filament with it. It only lights fragments the tube already covers, so it
// stays inside the declared reach.

const float kFlareSeconds = 0.35;

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
    float lifetime = clamp(p_lifetime, 0.05, kTrailSeconds);
    float halfWidth = 0.5 * max(p_width, 0.5) * scale;
    float sigma = halfWidth * 1.6 + 1.5 * scale;
    float innerSigma = sigma * 0.5;
    // Four sigma, where the bloom is under a thousandth. Bounded by the reach
    // too, since four sigma at the widest stroke is past it.
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
    // Never wider than the half-width it feathers: past that the smoothstep's
    // inner edge goes negative and the core stops reaching full alpha even at
    // the centre of the stroke, so the thinnest settings come out washed out
    // rather than thin. Only binds below about two logical px of width.
    float feather = min(0.75 + 1.6 * smoothstep(0.0, 1200.0 * scale, pointerFilteredSpeed()), halfWidth);

    // How far apart the colours sit, 0 (one white filament) to 1 (full ramp
    // edge to edge). Driven by the FILTERED speed for the reason the gate is:
    // the per-sample figure swings hard frame to frame and the spectrum would
    // breathe in and out while the hand moved evenly.
    float spreadSpeed = max(p_spreadSpeed, 1.0) * scale;
    float activity = clamp(pointerFilteredSpeed() / spreadSpeed, 0.0, 1.0);

    // Press flare: the split snaps open and the filament brightens.
    float flare = 0.0;
    float sincePress = pointerSincePress();
    if (p_clickFlare > 0.0 && uPointerPress.w > 0.5 && sincePress < kFlareSeconds) {
        float ct = sincePress / kFlareSeconds;
        flare = (1.0 - ct) * (1.0 - ct) * p_clickFlare;
    }

    // The dispersion actually applied. The 0.06 floor keeps the ramp from
    // collapsing exactly onto its own middle at a standstill, which would
    // leave the side coordinate meaningless. It no longer shows as colour at
    // the edges: the same factor now drives the white collapse further down,
    // so a slow drag reads as the white filament the description promises.
    // The parameter at 0 still removes the split entirely.
    float disp = clamp(p_dispersion, 0.0, 1.0) * clamp(0.06 + 0.94 * activity + 0.5 * flare, 0.0, 1.0);

    float core = 0.0;
    float hot = 0.0;
    float halo = 0.0;
    float bestCover = 0.0;
    float bestSide = 0.0;
    float bestAge = 0.0;

    // Only the LIVE run of samples is drawn or smoothed over: at a lifetime
    // equal to the metadata trailSeconds the samples behind it are outside
    // the damage rect, and the smoothing kernel would otherwise blend a
    // segment's far end toward one of them.
    int live = pointerLiveCount(count, lifetime);
    if (live < 2) {
        return vec4(0.0);
    }
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
        // distance maths. The curve can leave the box of its control points,
        // so the box the cull is done on has to allow for it or a fragment the
        // stroke genuinely covers is skipped and the stroke is clipped.
        if (pointerSegmentOutside(px, i, live, a, b, limit + pointerCurveBulge(c0, c1, c2, c3))) {
            pointerCurveAdvance(i + 3, live, p_smoothing);
            continue;
        }
        if (distance(a.xy, b.xy) < 1.0) {
            // A stationary pair (raw positions under a pixel apart, the
            // sampler's own rest-slot rule) has no tube to draw, and a point
            // segment at age zero would hold a full-brightness dot at the rest
            // point where the compositor lets it age out. Tested on the raw
            // samples, since the smoothing kernel pulls the pair's endpoints
            // apart toward the neighbour beyond.
            pointerCurveAdvance(i + 3, live, p_smoothing);
            continue;
        }
        float t;
        float d = pointerCurveDistanceFrom(px, c0, c1, c2, c3, t);
        // The span's control points are kept while the window advances, so
        // the cull below can come FIRST. Most fragments inside a reject box
        // are still outside the tube, and the side costs a second curve
        // evaluation, a length and a divide that they would otherwise all pay.
        vec2 s0 = c0;
        vec2 s1 = c1;
        vec2 s2 = c2;
        vec2 s3 = c3;
        pointerCurveAdvance(i + 3, live, p_smoothing);
        if (d > limit) {
            continue;
        }
        // Which side of the stroke the fragment sits on, measured against the
        // CURVE's local direction rather than the chord's. On a rounded corner
        // the two disagree by the whole turn angle, and the spectrum would
        // twist through the bend. The tangent is a short forward difference
        // along the span, the same construction the distance walk uses.
        vec2 here = pointerCurvePoint(s0, s1, s2, s3, t);
        vec2 ahead = pointerCurvePoint(s0, s1, s2, s3, min(t + 0.05, 1.0));
        vec2 seg = ahead - here;
        vec2 rel = px - here;
        // The 2D cross product, normalised by the tangent length so it is a
        // signed DISTANCE rather than an area, which is what the spectrum
        // coordinate wants. A degenerate tangent (t already at the span's end
        // on a span whose ends coincide) falls back to no side, which reads as
        // the middle of the ramp rather than as an edge colour.
        float tangentLen = length(seg);
        float side = tangentLen > 1e-4 ? (seg.x * rel.y - seg.y * rel.x) / tangentLen : 0.0;
        float age = clamp(mix(a.z, b.z, t) / lifetime, 0.0, 1.0);
        float u = 1.0 - age;
        float fade = u * u;
        float c = (1.0 - smoothstep(halfWidth - feather, halfWidth + feather, d)) * fade;
        // The white filament down the middle. Narrow enough that the spectrum
        // still reaches full saturation at the stroke's edges.
        float ht = exp(-(d * d) / (2.0 * max(halfWidth * 0.3, 0.7 * scale) * max(halfWidth * 0.3, 0.7 * scale))) * fade;
        // Two lobes sharing one peak rather than one wide gaussian: the same
        // outer limit, but more of the light close to the stroke, which is the
        // contrast that makes it read as bright.
        float h = (0.62 * exp(-(d * d) / (2.0 * innerSigma * innerSigma))
                   + 0.38 * exp(-(d * d) / (2.0 * sigma * sigma)))
                  * fade * 0.45 * max(p_glow, 0.0);
        float thisCover = max(c, h);
        if (thisCover > bestCover) {
            bestCover = thisCover;
            bestSide = side;
            bestAge = age;
        }
        core = max(core, c);
        hot = max(hot, ht);
        halo = max(halo, h);
    }

    float cover = max(core, halo);
    if (cover <= 0.0) {
        return vec4(0.0);
    }

    // The spectrum coordinate: the signed offset mapped across the stroke,
    // squeezed toward the middle as the dispersion closes. The stroke's own
    // half-width is the reference, not the bloom's, so the ramp reaches both
    // ends inside the solid part of the stroke and the bloom carries the end
    // colours outward.
    float sideNorm = clamp(bestSide / max(halfWidth, 1e-4), -1.0, 1.0);
    float ramp = clamp(0.5 + 0.5 * sideNorm * disp, 0.0, 1.0);
    // A slow drift along the length so a long stroke is not one flat ramp.
    // Clamped rather than wrapped: wrapping puts rose next to cyan at the
    // seam, the one maximum-contrast join the brand ramp has no return leg
    // for.
    vec3 rgb = phosphorGradient(clamp(ramp * 0.9 + 0.1 * bestAge, 0.0, 1.0));
    // As the split closes, the ramp collapses onto its own middle, which is
    // mid-spectrum violet rather than white. The stroke is only the single
    // white filament the description promises if the COLOUR collapses with
    // it, so fold the same factor through here.
    rgb = mix(vec3(1.0), rgb, clamp(disp / max(clamp(p_dispersion, 0.0, 1.0), 1e-4), 0.0, 1.0));
    // The filament is white where the colours have not separated, and the
    // press flare drives it whiter still.
    rgb = mix(rgb, vec3(1.0), clamp(hot * (0.9 - 0.35 * disp) + 0.5 * flare, 0.0, 1.0));

    float alpha = clamp(cover * (1.0 + 0.35 * flare), 0.0, 1.0) * gate;
    return premul(min(rgb * (1.0 + 0.8 * flare), vec3(1.0)), alpha);
}
