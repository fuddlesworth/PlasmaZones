// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared helpers for POINTER shader packs — the pointer-family cousin of
// data/surface/shared/surface_lib.glsl. Pulls in the uniform contract
// (pointer_uniforms.glsl) and layers the idioms every pointer pack would
// otherwise re-derive inline: pixel-space lookup, trail access, segment
// distance, event timers, the brand gradient and premultiplied output.
// Noise helpers live in the opt-in pointer_noise.glsl module.
//
// Runtime-agnostic: every helper reads only the contract uniforms, which are
// global in both the compositor (default-block) and preview (UBO) branches.
// This is what the registry's entry prologue includes, so a pPointer body
// sees everything here without an include of its own.

#ifndef PLASMAZONES_POINTER_LIB_GLSL
#define PLASMAZONES_POINTER_LIB_GLSL

#include <pointer_uniforms.glsl>

const float TAU = 6.28318530718;
const int kPointerTrailCapacity = 32;

// Logical-to-device scale. Multiply a pack's logical-px parameter by this to
// reach the device-px canvas the position uniforms use.
float pointerScale() {
    // Fall back to unscaled, not to a floor. Every use is a multiply, so there
    // is no divide to protect, and a 0.001 answer would paint the whole pack at
    // a thousandth of its size — invisible — where 1.0 merely means "no scale
    // information", which is what an unset uniform actually is.
    return uPointerState.z > 0.0 ? uPointerState.z : 1.0;
}

// This pack's resolved reach in DEVICE px: the radius the host inflates the
// damage rect by around every live sample, and so the furthest anything
// painted can be from one and still reach the screen. The same number the
// pack's metadata `reach` / `reachParam` declares, after the host resolves it
// against the user's parameter values and scales it — read it from here rather
// than mirroring the metadata by hand, so the two cannot drift.
float pointerReach() {
    // Floored, not merely clamped non-negative, for the reason pointerScale
    // gives its own fallback: an unset uniform reads 0, and every caller uses
    // the result as the outer edge of a falloff window. smoothstep with equal
    // edges is undefined in GLSL (it divides by edge1 - edge0), so a host that
    // failed to push this would not dim the pack, it would hand the driver
    // undefined behaviour. The host's own floor is kMinReach, and one device px
    // is the smallest value that still turns a point into a region.
    return max(uPointerFlags.y, 1.0);
}

// The standard falloff across a pack's reach: 1 at the sample, 0 at the edge,
// with the last fifth of the radius as the feather. The packs that measure
// against the reach had this spelled out inline. Keeping it here is what makes
// the reach contract one rule rather than a copy per pack, and gives the
// edge-case floor above a single place to matter.
float pointerReachWindow(float d, float reach) {
    return 1.0 - smoothstep(reach * 0.8, reach, d);
}

// This fragment's canvas position, TOP-DOWN device px. `uv` is the incoming
// vTexCoord. The compositor's render target is bottom-origin (Y-up), so the Y
// is flipped there to reach the top-down space the position uniforms use.
vec2 pointerPixel(vec2 uv) {
#ifdef PLASMAZONES_KWIN
    return vec2(uv.x, 1.0 - uv.y) * iResolution;
#else
    return uv * iResolution;
#endif
}

// Number of trail samples actually filled this frame (0..32).
int pointerTrailCount() {
    return clamp(int(uPointerState.w + 0.5), 0, kPointerTrailCapacity);
}

// Trail sample i (0 = newest). .xy canvas px, .z age seconds, .w speed.
// Indices in [count, 31] read the contract's zero entry; anything outside
// 0..31 is clamped into the array (a negative index reads the newest sample),
// never out of bounds.
vec4 pointerTrailAt(int i) {
    return uPointerTrail[clamp(i, 0, kPointerTrailCapacity - 1)];
}

// Distance from canvas point `p` to the segment a..b, with `t` the normalized
// position along it (0 at `a`). A degenerate segment collapses to a point
// distance with t = 0. The index-taking helpers below are thin wrappers over
// this one; a pack that already holds both endpoints (it needs them for its
// own reject box) calls this directly rather than paying for the lookups a
// second time.
float pointerSegmentDistanceFrom(vec2 p, vec2 a, vec2 b, out float t) {
    vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 < 1e-6) {
        t = 0.0;
        return length(p - a);
    }
    t = clamp(dot(p - a, ab) / len2, 0.0, 1.0);
    return length(p - (a + ab * t));
}

// Distance from canvas point `p` to the trail segment trail[i]..trail[i+1],
// with `t` the normalized position along it (0 at trail[i], the newer end).
// Callers guard i + 1 < pointerTrailCount().
float pointerSegmentDistance(vec2 p, int i, out float t) {
    return pointerSegmentDistanceFrom(p, pointerTrailAt(i).xy, pointerTrailAt(i + 1).xy, t);
}

// A rate in cycles per second nudged to the nearest value that completes a
// whole number of cycles per iTime wrap.
//
// In the PREVIEW iTime wraps at 1024 s (kShaderTimeWrap in BaseUniforms.h).
// The overlay family rides the wrap through iTimeHi, but the pointer contract
// never sets that counterpart, so a phase derived from iTime alone snaps at
// every wrap unless the rate divides the wrap period. Rounding `rate * 1024`
// to an integer makes it divide exactly, and the nudge is at most 1/2048
// cycles per second, below anything a user could pick out. On the COMPOSITOR
// iTime restarts at 0 for each burst of pointer activity and never wraps, so
// there the nudge is harmless and a phase simply begins again per burst. Use
// this for anything periodic that runs while the pointer rests; a hash seed
// stepped from iTime does not need it, since a re-roll at the wrap is just
// another re-roll.
const float kPointerTimeWrap = 1024.0;
float pointerWrapSafeRate(float rate) {
    return max(round(rate * kPointerTimeWrap), 1.0) / kPointerTimeWrap;
}

// Speed gate: 0 below `activationSpeed`, easing to 1 as the speed reaches
// twice it, so a pack fades in as the pointer accelerates and retreats as it
// slows. Both arguments are in the SAME unit; packs gate through
// pointerActivationGate(), which hands in the filtered speed and the
// parameter scaled to device px. Per-sample uPointerTrail[].w is the input
// only for something drawn AT that sample. An activationSpeed of 0 or less
// means no threshold at all and always returns 1, which is what a pack
// shipping the parameter at 0 relies on to keep its old behaviour.
float pointerSpeedGate(float speed, float activationSpeed) {
    if (activationSpeed <= 0.0) {
        return 1.0;
    }
    return smoothstep(activationSpeed, activationSpeed * 2.0, speed);
}

// Whether the smoothed trail segment i..i+1 can be skipped for the fragment
// at `px`: true when `px` lies outside the box of the RAW samples i-1..i+2
// (clamped into the filled window, as pointerSmoothedAt clamps them)
// inflated by `inflate`. A smoothed sample is a convex blend of its raw
// neighbours, so the smoothed segment lies inside that box; for a STRAIGHT
// segment the test is exact and never clips, and it costs four raw reads
// where the smoothing lookup would cost six. `a` and `b` are the raw samples
// i and i+1 the caller already holds.
//
// A CURVED span is NOT contained in this box. A caller tracing
// pointerCurveDistanceFrom MUST add pointerCurveBulge(c0, c1, c2, c3) to
// `inflate`, or the curve is culled where it genuinely covers the fragment
// and the stroke is clipped on exactly the rounded corners the curve exists
// to draw.
bool pointerSegmentOutside(vec2 px, int i, int count, vec4 a, vec4 b, float inflate) {
    vec2 rawPrev = pointerTrailAt(max(i - 1, 0)).xy;
    vec2 rawNext = pointerTrailAt(min(i + 2, count - 1)).xy;
    vec2 lo = min(min(a.xy, b.xy), min(rawPrev, rawNext)) - inflate;
    vec2 hi = max(max(a.xy, b.xy), max(rawPrev, rawNext)) + inflate;
    return any(lessThan(px, lo)) || any(greaterThan(px, hi));
}

// Trail sample i's position with its two neighbours blended in, by
// `smoothing` in 0..1, so a jittery hand still traces a clean curve. At 0 the
// raw sample comes back untouched. Neighbour reads are clamped into the
// filled window, so a sample at either end cannot pull an unfilled entry at
// the canvas origin into the average.
//
// The neighbours are weighted by INVERSE TIME DISTANCE, not equally.
//
// The gaps either side of a sample are not the same size. The ring's spacing
// is uniform in time, but the HEAD is refreshed in place between appends, so
// the newest gap is whatever fraction of the interval has passed rather than a
// whole one, and the run shortens at the tail as samples age out. An
// equal-weight kernel drags a sample toward whichever neighbour is further
// away in time, which is the one that says least about where the pointer
// actually went.
//
// It matters most at the head, where the near gap can be a fraction of a
// millisecond: index 0 is where the pointer IS, and equal weights dragged it a
// quarter of the way toward the sample behind it, putting the stroke visibly
// behind the cursor. Here a clamped end neighbour is the sample itself, so its
// time distance is zero, it takes essentially all the weight, and the endpoint
// is left alone.
//
// The strength is unchanged: the old kernel is exactly this one with both
// weights equal, so `smoothing` means what it always meant, and on the evenly
// spaced interior the two agree.
//
// Every pack that smooths a path MUST come through here rather than rolling
// its own kernel: two packs in one chain tracing visibly different curves
// from the same pointer would read as a bug.
vec2 pointerSmoothedAt(int i, int count, float smoothing) {
    int last = max(clamp(count, 0, kPointerTrailCapacity) - 1, 0);
    vec4 here = pointerTrailAt(clamp(i, 0, last));
    float s = clamp(smoothing, 0.0, 1.0);
    if (s <= 0.0) {
        return here.xy;
    }
    vec4 prev = pointerTrailAt(clamp(i - 1, 0, last));
    vec4 next = pointerTrailAt(clamp(i + 1, 0, last));
    // Ages are seconds and run newest-first, so index i - 1 is the YOUNGER
    // neighbour. Both gaps carry a small floor: a clamped end neighbour has a
    // gap of exactly zero, and the weights divide by these.
    float dtPrev = max(here.z - prev.z, 0.0) + 1e-4;
    float dtNext = max(next.z - here.z, 0.0) + 1e-4;
    // Weight w = 1/dt on each side, normalised. Written multiplied through by
    // dtPrev * dtNext so it costs one divide rather than three.
    vec2 mean = (prev.xy * dtNext + next.xy * dtPrev) / (dtPrev + dtNext);
    return mix(here.xy, (here.xy + mean) * 0.5, s);
}

// pointerSegmentDistance over the smoothed path: distance from `p` to the
// segment between smoothed samples i and i + 1, with `t` its normalized
// position along that segment (0 at the newer end). A drop-in for a caller
// that gains a `smoothing` parameter; at smoothing 0 it is the raw-path
// answer. No bundled pack uses it (they all hold the smoothed endpoints for
// their own reject box and call pointerSegmentDistanceFrom); it is kept as
// part of the public helper set for third-party packs.
float pointerSmoothSegmentDistance(vec2 p, int i, int count, float smoothing, out float t) {
    return pointerSegmentDistanceFrom(p, pointerSmoothedAt(i, count, smoothing),
                                      pointerSmoothedAt(i + 1, count, smoothing), t);
}

// ── Curved path ─────────────────────────────────────────────────────────────
//
// WHY THIS EXISTS. The history is spaced by the pack's trail window over
// kPointerTrailCapacity slots (PointerHistory::setTrailSeconds), so at the
// usual 0.9 s window one sample lands every 30 ms (the spacing is a ceil of
// window / 31, so 0.9 s rounds up from 29.03). Sweep the pointer at
// 1500 px/s and consecutive samples are 45 px apart, and a pack that strokes
// straight segments between them draws exactly that: a chain of long straight
// chords meeting at angular corners. The faster the hand moves the more
// obviously the stroke is a polygon, which is the one artefact every path
// pack in this family shared.
//
// pointerSmoothedAt does not fix it and was never meant to. It MOVES the
// vertices to denoise a shaky hand; it does not add any, so the path keeps
// the same corner count and the corners stay corners.
//
// The curve is a Catmull-Rom spline through the smoothed samples, walked as
// kPointerCurveSteps straight pieces. It INTERPOLATES the samples, so the
// stroke still passes through where the pointer actually was — a B-spline
// would have guaranteed hull containment for free but cuts corners visibly,
// and a trail that does not go where the pointer went is a worse bug than a
// faceted one.
//
// Every path pack MUST trace the path through here, for the reason
// pointerSmoothedAt gives: two packs in one chain drawing visibly different
// curves from the same pointer would read as a bug.

// Straight pieces per span. Four is where the faceting stops being visible at
// the spacing above; the cost is per span per fragment, behind the caller's
// reject box, so this is not free and should not be raised casually. The
// price is concrete: a span that survives the cull costs four
// pointerSegmentDistanceFrom calls plus three pointerCurvePoint evaluations
// where the straight-segment path this replaced cost one call, so roughly
// four to five times the inner loop, on seven packs, per fragment, per frame.
//
// This is the one quantity in the curve path that does NOT scale, and the
// spacing it was measured against is a SCALE-1 figure. Everything the curve is
// judged against -- half width, sigma, the bulge, the reach -- is in device px
// and grows with the display, while the chords the four pieces divide grow
// too, so each piece covers twice the device px on a 2x output. The faceting
// is therefore at its worst on HiDPI, which is the opposite of what the
// paragraph above reads like. Deriving the count from the chord length would
// fix that at the price of a non-constant loop bound on seven packs, so it is
// left alone deliberately rather than by omission.
const int kPointerCurveSteps = 4;
// Catmull-Rom tension. The textbook value is 0.5; this is half of it, which
// halves how far the curve can bulge outside the box of its four control
// points (see pointerCurveBulge) while still visibly rounding the corners.
// The overshoot is what the reject boxes and the reach budget have to pay
// for, so it is bought deliberately and cheaply.
const float kPointerCurveTension = 0.25;

// The most the span c1..c2 can leave the box of its two ENDPOINTS c1 and c2,
// in the same px the points are in. A Catmull-Rom span is a cubic whose
// Hermite tangents are the tension times the neighbour chords, and the Bezier
// control points it is equivalent to sit one third of a tangent off each
// endpoint, so this bounds the hull the curve is guaranteed to stay inside.
//
// The two-endpoint reading is the load-bearing one, not a four-point one.
// pointerSegmentOutside builds its box from the RAW samples i-1..i+2, which
// contains c1 and c2 (each a convex blend of raw neighbours inside that
// window) but NOT c0 and c3, whose blends reach raw i-2 and i+3. The
// neighbours enter only through the tangent magnitudes, which is precisely
// what this bound measures, so raw box + this value does contain the curve.
// Read as a four-point guarantee the cull looks unsound and someone widens a
// box that seven packs pay for per span per fragment. Hand it to
// pointerSegmentOutside on top of the caller's own inflate, or a fragment the
// curve genuinely covers can be culled and the stroke clipped.
float pointerCurveBulge(vec2 c0, vec2 c1, vec2 c2, vec2 c3) {
    // Component max, not length(). Every caller hands this to an AXIS-ALIGNED
    // box, so the bound only has to cover each axis separately, and the
    // excursion along an axis is at most a third of that axis's component of
    // the longer tangent. That is both cheaper -- this is evaluated for every
    // span for every fragment, ahead of the cull it feeds, so two sqrt here
    // are two sqrt on the cheap path of seven packs -- and TIGHTER, because a
    // vector's largest component never exceeds its length.
    vec2 t1 = abs(c2 - c0);
    vec2 t2 = abs(c3 - c1);
    return kPointerCurveTension * (1.0 / 3.0) * max(max(t1.x, t1.y), max(t2.x, t2.y));
}

// Advance the four-point control window by one span.
//
// Every path pack walks its run with the same rolling window named c0..c3, and
// has to shift it at EVERY exit from an iteration -- the cull's continue, the
// stationary-pair continue, and the fall-through. That was twenty copies of
// four assignments across seven packs, where dropping one line desynchronises
// the window from the span with no compile error and no symptom beyond a
// subtly wrong curve. One name, so a site is either right or absent.
//
// A macro rather than a function because the window lives in the caller's
// locals. GLSL `inout` would work but would name the four points twice at
// every site, which is the thing being removed.
//
// It expands to four statements, so it needs a braced body: never write it as
// the whole of a brace-less `if`, or only the first assignment is conditional.
#define pointerCurveAdvance(nextIndex, live, smoothing) \
    c0 = c1;                                           \
    c1 = c2;                                           \
    c2 = c3;                                           \
    c3 = pointerSmoothedAt((nextIndex), (live), (smoothing))

// Point at `t` in 0..1 along the Catmull-Rom span between c1 and c2, with c0
// and c3 the neighbours that set the tangents. A caller at either end of the
// run passes the endpoint twice, which makes that end's tangent the chord
// itself (m = tension * (c2 - c1), not zero), so the span leaves the endpoint
// straight rather than curling back into a run that is not there.
vec2 pointerCurvePoint(vec2 c0, vec2 c1, vec2 c2, vec2 c3, float t) {
    vec2 m1 = kPointerCurveTension * (c2 - c0);
    vec2 m2 = kPointerCurveTension * (c3 - c1);
    float t2 = t * t;
    float t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * c1 + (t3 - 2.0 * t2 + t) * m1
           + (-2.0 * t3 + 3.0 * t2) * c2 + (t3 - t2) * m2;
}

// Nearest distance from `p` to the Catmull-Rom span between c1 and c2, with
// `t` the normalised position along the WHOLE span (0 at c1, 1 at c2) of the
// closest point found. The counterpart of pointerSegmentDistanceFrom, and a
// drop-in for it in a pack that already carries its own endpoints: the
// straight-segment call becomes this one with the two neighbours added.
//
// `t` is the piecewise-linear parameter, not arc length. Callers use it to
// interpolate the endpoint AGES and per-sample SPEEDS across the span, where
// the two differ by less than a frame. It is never a distance.
float pointerCurveDistanceFrom(vec2 p, vec2 c0, vec2 c1, vec2 c2, vec2 c3, out float t) {
    float best = 1e9;
    t = 0.0;
    vec2 prev = c1;
    for (int k = 1; k <= kPointerCurveSteps; ++k) {
        float tk = float(k) / float(kPointerCurveSteps);
        // The last piece ends exactly on c2 rather than on the basis evaluated
        // at 1, so consecutive spans cannot leave a hairline gap between them
        // from floating-point drift at the join.
        vec2 next = (k == kPointerCurveSteps) ? c2 : pointerCurvePoint(c0, c1, c2, c3, tk);
        float sub;
        float d = pointerSegmentDistanceFrom(p, prev, next, sub);
        if (d < best) {
            best = d;
            t = (float(k - 1) + sub) / float(kPointerCurveSteps);
        }
        prev = next;
    }
    return best;
}

// Seconds since the pointer last moved.
float pointerIdleSeconds() {
    return max(uPointerState.y, 0.0);
}

// Seconds since the last button press (a large value when none yet).
float pointerSincePress() {
    return max(uPointerPress.z, 0.0);
}

// Seconds since the last button release (a large value when none yet).
float pointerSinceRelease() {
    return max(uPointerRelease.z, 0.0);
}

// Brand spectrum: cyan #22D3EE → blue #3B82F6 → purple #A855F7 → rose
// #F43F5E, piecewise mixed over t in 0..1 (clamped).
vec3 phosphorGradient(float t) {
    const vec3 cyan = vec3(0.133, 0.827, 0.933);
    const vec3 blue = vec3(0.231, 0.510, 0.965);
    const vec3 purple = vec3(0.659, 0.333, 0.969);
    const vec3 rose = vec3(0.957, 0.247, 0.369);
    t = clamp(t, 0.0, 1.0) * 3.0;
    if (t < 1.0) {
        return mix(cyan, blue, t);
    }
    if (t < 2.0) {
        return mix(blue, purple, t - 1.0);
    }
    return mix(purple, rose, t - 2.0);
}

// Number of trail samples, newest first, younger than `maxAge`: the LIVE
// run a pack draws and smooths over. Ages are monotonic in the index, so the
// walk ends at the first old sample. Hand this to pointerSmoothedAt as its
// `count` so the kernel clamps its neighbours into the live run: the ring is
// never purged, and at a window equal to the metadata trailSeconds the
// samples behind the run are outside the damage rect, where a blended
// endpoint would leave a sliver frozen on the compositor.
int pointerLiveCount(int count, float maxAge) {
    int live = 0;
    for (int i = 0; i < kPointerTrailCapacity; ++i) {
        if (i >= count || pointerTrailAt(i).z >= maxAge) {
            break;
        }
        live = i + 1;
    }
    return live;
}

// Premultiplied output from a straight colour and coverage.
vec4 premul(vec3 rgb, float a) {
    // Both halves clamped. The blend is GL_ONE / GL_ONE_MINUS_SRC_ALPHA, which
    // requires every channel to be at or below the alpha; a pack that hands in
    // an over-bright colour would otherwise return a channel greater than its
    // own coverage and add light it never claimed.
    a = clamp(a, 0.0, 1.0);
    return vec4(clamp(rgb, 0.0, 1.0) * a, a);
}

// Premultiplied output from an ADDITIVE accumulation: `rgb` is the sum of
// colour times coverage over every shape and `alpha` the sum of coverages.
// Overlaps push the sum past 1; the coverage is clamped and the colour kept
// in proportion, so the result stays a valid premultiplied colour. Zero when
// nothing accumulated.
vec4 premulAccumulated(vec3 rgb, float alpha) {
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    float clamped = min(alpha, 1.0);
    return vec4(clamp(rgb * (clamped / alpha), 0.0, clamped), clamped);
}

// Pointer speed with the per-sample jitter filtered out, in px/s.
//
// `uPointerTrail[].w` is the INSTANTANEOUS speed of one sample, distance over
// the frame delta that produced it. Frame deltas are wall-clock and hitch
// under load, so that figure swings hard from sample to sample even when the
// hand is moving evenly. Gating anything on it directly makes the gate chatter
// and the pack flickers on and off, which is exactly how the windtrail pack
// first shipped.
//
// The host's sampler computes this once per frame (PointerHistory::
// filteredSpeed): the exponential filter the upstream windtrail effect runs
// on its own sampler (a = 0.46), walked over the CURRENT STROKE only, so one
// loud sample moves the answer a little rather than deciding it, and the
// speeds of a stroke before a pause never seed the gate of the next one.
// The sampler knows the event times, so it can tell a pause from a long
// sample interval where a shader-side walk over sample ages could not. It
// arrives in uPointerVelocity.w. Use this for anything a user would notice
// switching, above all pointerSpeedGate(). Per-sample `.w` is still the
// right input for something drawn AT that sample, such as how wide the
// ribbon was where the pointer actually was.
float pointerFilteredSpeed() {
    return max(uPointerVelocity.w, 0.0);
}

// Speed gate on the filtered speed for a pack's `activationSpeed` parameter.
// The one place a pack should gate from, so every pack in a chain opens and
// closes on the same figure. The parameter is LOGICAL px per second, as its
// metadata says, and the filtered speed is device px/s, so the threshold is
// scaled here: the same hand motion opens the gate at the same setting on a
// 1x and a 2x display. A pack's own speed constants (the speed at which it
// is fully grown, fully wide, thinnest) are logical too and scale the same
// way at their use.
float pointerActivationGate(float activationSpeed) {
    return pointerSpeedGate(pointerFilteredSpeed(), activationSpeed * pointerScale());
}

// The per-button colour a click pack paints with: left, right, middle by the
// button code the press and release uniforms carry (1, 2, 3).
vec4 pointerButtonColour(float button, vec4 left, vec4 right, vec4 middle) {
    if (button > 2.5) {
        return middle;
    }
    if (button > 1.5) {
        return right;
    }
    return left;
}

#endif // PLASMAZONES_POINTER_LIB_GLSL
