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
// with the last fifth of the radius as the feather. Four packs had this
// spelled out inline; keeping it here is what makes the reach contract one
// rule rather than four copies, and gives the edge-case floor above a single
// place to matter.
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
// neighbours, so the smoothed segment lies inside that box; the test is
// exact and never clips, and it costs four raw reads where the smoothing
// lookup would cost six. `a` and `b` are the raw samples i and i+1 the
// caller already holds.
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
// Every pack that smooths a path MUST come through here rather than rolling
// its own kernel: two packs in one chain tracing visibly different curves
// from the same pointer would read as a bug.
vec2 pointerSmoothedAt(int i, int count, float smoothing) {
    int last = max(clamp(count, 0, kPointerTrailCapacity) - 1, 0);
    vec2 here = pointerTrailAt(clamp(i, 0, last)).xy;
    float s = clamp(smoothing, 0.0, 1.0);
    if (s <= 0.0) {
        return here;
    }
    vec2 prev = pointerTrailAt(clamp(i - 1, 0, last)).xy;
    vec2 next = pointerTrailAt(clamp(i + 1, 0, last)).xy;
    return mix(here, (prev + here * 2.0 + next) * 0.25, s);
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
    return vec4(rgb * (clamped / alpha), clamped);
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
