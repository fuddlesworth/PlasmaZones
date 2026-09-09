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
// Out-of-range indices read the zero entry, never out of bounds.
vec4 pointerTrailAt(int i) {
    return uPointerTrail[clamp(i, 0, kPointerTrailCapacity - 1)];
}

// Distance from canvas point `p` to the trail segment trail[i]..trail[i+1],
// with `t` the normalized position along it (0 at trail[i], the newer end).
// Callers guard i + 1 < pointerTrailCount(); a degenerate segment collapses
// to a point distance with t = 0.
float pointerSegmentDistance(vec2 p, int i, out float t) {
    vec2 a = pointerTrailAt(i).xy;
    vec2 b = pointerTrailAt(i + 1).xy;
    vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 < 1e-6) {
        t = 0.0;
        return length(p - a);
    }
    t = clamp(dot(p - a, ab) / len2, 0.0, 1.0);
    return length(p - (a + ab * t));
}

// Speed gate for a path sample: 0 below `activationSpeed`, easing to 1 as the
// sample reaches twice it, so a pack fades in as the pointer accelerates and
// retreats as it slows. `speed` is device px/s, from uPointerVelocity.z or a
// sample's uPointerTrail[].w. An activationSpeed of 0 or less means no
// threshold at all and always returns 1, which is what a pack shipping the
// parameter at 0 relies on to keep its old behaviour.
float pointerSpeedGate(float speed, float activationSpeed) {
    if (activationSpeed <= 0.0) {
        return 1.0;
    }
    return smoothstep(activationSpeed, activationSpeed * 2.0, speed);
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
// position along that segment (0 at the newer end). A drop-in for callers
// that gain a `smoothing` parameter; at smoothing 0 it is the raw-path
// answer.
float pointerSmoothSegmentDistance(vec2 p, int i, int count, float smoothing, out float t) {
    vec2 a = pointerSmoothedAt(i, count, smoothing);
    vec2 b = pointerSmoothedAt(i + 1, count, smoothing);
    vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 < 1e-6) {
        t = 0.0;
        return length(p - a);
    }
    t = clamp(dot(p - a, ab) / len2, 0.0, 1.0);
    return length(p - (a + ab * t));
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

// Premultiplied output from a straight colour and coverage.
vec4 premul(vec3 rgb, float a) {
    // Both halves clamped. The blend is GL_ONE / GL_ONE_MINUS_SRC_ALPHA, which
    // requires every channel to be at or below the alpha; a pack that hands in
    // an over-bright colour would otherwise return a channel greater than its
    // own coverage and add light it never claimed.
    a = clamp(a, 0.0, 1.0);
    return vec4(clamp(rgb, 0.0, 1.0) * a, a);
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
// This walks the ring oldest to newest with the same exponential filter the
// upstream windtrail effect uses on its own sampler
// (`filtered = filtered * (1 - a) + raw * a`, a = 0.46), so one loud sample
// moves the answer a little rather than deciding it. Use this for anything a
// user would notice switching, above all pointerSpeedGate(). Per-sample `.w`
// is still the right input for something drawn AT that sample, such as how
// wide the ribbon was where the pointer actually was.
float pointerFilteredSpeed() {
    int count = pointerTrailCount();
    if (count < 1) {
        return 0.0;
    }
    // Seeded from the oldest sample rather than 0, or a short ring would
    // always report a speed biased down toward standing still.
    float filtered = pointerTrailAt(count - 1).w;
    for (int i = kPointerTrailCapacity - 2; i >= 0; --i) {
        if (i > count - 2) {
            continue;
        }
        filtered = filtered * 0.54 + pointerTrailAt(i).w * 0.46;
    }
    return max(filtered, 0.0);
}

#endif // PLASMAZONES_POINTER_LIB_GLSL
