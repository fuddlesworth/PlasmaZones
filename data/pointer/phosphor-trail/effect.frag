// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Trail pointer shader — one smooth tube of light along the recent
// path. A thin bright core sits inside a soft bloom, so the stroke reads as
// light rather than as paint, and it holds the same width from end to end.
// Only the brightness falls with age, which is what keeps its silhouette
// clear of the comet, whose tail narrows to a point behind a bright head.
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
// `activationSpeed` and `smoothing` come from the shared pointerSpeedGate()
// and pointerSmoothedAt(), so this pack, Comet, Sparks and WindTrail all
// threshold and smooth identically. Both default to 0, which is the
// no-threshold, raw-path behaviour.

const int kMaxTrail = 32;
const float kFlareSeconds = 0.4;

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 2) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float lifetime = max(p_lifetime, 0.05);
    float halfWidth = 0.5 * max(p_width, 0.5) * scale;
    float sigma = halfWidth * 2.2 + 1.5 * scale;

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
    // (pointerWrapSafeRate) so the colour does not jump when iTime wraps.
    float cycle = max(p_colorCycle, 0.0);
    float hueBase = cycle > 0.0 ? abs(fract(iTime * pointerWrapSafeRate(1.0 / (cycle * 2.0))) * 2.0 - 1.0) : 0.35;

    float core = 0.0;
    float halo = 0.0;
    float hueAge = 0.0;

    for (int i = 0; i < kMaxTrail - 1; ++i) {
        if (i + 1 >= count) {
            break;
        }
        vec4 a = pointerTrailAt(i);
        vec4 b = pointerTrailAt(i + 1);
        if (a.z >= lifetime) {
            break;
        }
        float gate = min(pointerSpeedGate(a.w, p_activationSpeed), pointerSpeedGate(b.w, p_activationSpeed));
        if (gate <= 0.0) {
            continue;
        }
        float t;
        float d = pointerSmoothSegmentDistance(px, i, count, p_smoothing, t);
        // Four sigma, where the bloom is under a thousandth, rather than three,
        // where it was still a visible 1.5% and cut off square. Bounded by the
        // reach too, since four sigma at the widest stroke is past it.
        if (d > min(sigma * 4.0, pointerReach())) {
            continue;
        }
        float age = clamp(mix(a.z, b.z, t) / lifetime, 0.0, 1.0);
        float fade = (1.0 - age) * (1.0 - age) * gate;
        // The antialias feather is DEVICE px and stays unscaled, the family
        // convention: scaling it makes the stroke's edge twice as soft on a 2x
        // display as every sibling pack's.
        float c = (1.0 - smoothstep(halfWidth - 0.75, halfWidth + 0.75, d)) * fade;
        float h = exp(-(d * d) / (2.0 * sigma * sigma)) * fade * 0.45 * max(p_glow, 0.0);
        if (max(c, h) > max(core, halo)) {
            hueAge = age;
        }
        core = max(core, c);
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
    // The base is scaled to leave room for the offset so the walk still
    // reaches both ends.
    vec3 rgb = phosphorGradient(clamp(hueBase * 0.96 + 0.04 * hueAge, 0.0, 1.0));
    // The core is close to white at its centre, which is what makes it read as
    // light instead of as a coloured line.
    float whiten = core * 0.55;
    rgb = mix(rgb, vec3(1.0), clamp(whiten + 0.6 * flare, 0.0, 1.0));

    float alpha = clamp(cover * (1.0 + 0.5 * flare), 0.0, 1.0);
    return premul(min(rgb * (1.0 + 1.2 * flare), vec3(1.0)), alpha);
}
