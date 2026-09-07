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
    return max(uPointerState.z, 0.001);
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
    a = clamp(a, 0.0, 1.0);
    return vec4(rgb * a, a);
}

#endif // PLASMAZONES_POINTER_LIB_GLSL
