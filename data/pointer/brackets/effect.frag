// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Brackets pointer shader — four L-shaped corner marks that snap inward onto
// the point you clicked, hold there, and fade. Nothing here reads the trail or
// the velocity, so the pack paints exactly nothing while the pointer is merely
// moving: every contribution is gated on uPointerPress / uPointerRelease.
//
// All lengths derive from p_spread, which is the pack's reachParam, and each
// event is additionally clipped to a box of half-extent `spread` around its
// own origin, so the pack cannot paint outside the damage rect the host buys.

const float kTrailSeconds = 1.2;
const float kConvergeFraction = 0.42; // share of `duration` spent closing in
const float kCornerFraction = 0.70;   // starting corner offset, as a share of spread
const float kArmMax = 0.9;            // arm length cap, as a share of the corner offset

vec4 buttonColour(float button) {
    if (button > 2.5) {
        return p_colorMiddle;
    }
    if (button > 1.5) {
        return p_colorRight;
    }
    return p_colorLeft;
}

// Distance from `q` to the straight segment a..b. Written out rather than
// reusing the trail helpers, which work on pointer history and not on marks.
float armDistance(vec2 q, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 < 1e-6) {
        return length(q - a);
    }
    float t = clamp(dot(q - a, ab) / len2, 0.0, 1.0);
    return length(q - (a + ab * t));
}

// Coverage of the four corner brackets for a point `q` already relative to the
// press point and already un-rotated. `d` is the corner offset on each axis,
// `arm` the arm length, `half_` the half line thickness. All device px.
float brackets(vec2 q, float d, float arm, float half_) {
    float cover = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 s = vec2((i == 1 || i == 3) ? 1.0 : -1.0, (i < 2) ? -1.0 : 1.0);
        vec2 corner = s * d;
        // Both arms run from the corner back toward the press point.
        float dist = armDistance(q, corner, corner - vec2(s.x * arm, 0.0));
        dist = min(dist, armDistance(q, corner, corner - vec2(0.0, s.y * arm)));
        cover = max(cover, 1.0 - smoothstep(half_ - 0.75, half_ + 0.75, dist));
    }
    return cover;
}

// One complete response to a button event: converge, hold, fade. Returns
// coverage in 0..1 and is exactly 0 at and past `duration`.
float response(vec2 px, vec2 origin, float since, float duration, float spread, float half_) {
    if (since < 0.0 || since >= duration) {
        return 0.0;
    }
    float converge = duration * kConvergeFraction;
    float hold = clamp(p_hold, 0.0, max(duration - converge - 0.05, 0.0));

    // Ease-out convergence: quick at the start, decelerating into the lock.
    float k = clamp(since / max(converge, 1e-4), 0.0, 1.0);
    float ease = 1.0 - (1.0 - k) * (1.0 - k) * (1.0 - k);

    float start = spread * kCornerFraction;
    float rest = start * clamp(p_rest, 0.0, 1.0);
    float d = mix(start, rest, ease);
    float arm = min(spread * clamp(p_arm, 0.0, 1.0), d * kArmMax);

    // A small turn that unwinds as the marks arrive, so they settle square.
    float turn = radians(clamp(p_spin, 0.0, 12.0)) * (1.0 - ease);
    float c = cos(turn);
    float s = sin(turn);
    vec2 rel = px - origin;
    vec2 q = vec2(c * rel.x + s * rel.y, -s * rel.x + c * rel.y);

    float cover = brackets(q, d, arm, half_);

    // Full through the convergence and the hold, then square-eased to zero
    // exactly at `duration`.
    float tail = max(duration - converge - hold, 1e-4);
    float u = clamp((since - converge - hold) / tail, 0.0, 1.0);
    float fade = (1.0 - u) * (1.0 - u);
    // A very short ramp in, so the marks arrive rather than pop.
    fade *= smoothstep(0.0, 0.04, since);
    return cover * fade;
}

vec4 pPointer(vec2 uv) {
    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float duration = clamp(p_duration, 0.15, kTrailSeconds);
    float spread = max(p_spread, 16.0) * scale;
    float half_ = 0.5 * clamp(p_thickness, 0.5, 6.0) * scale;

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;

    float sincePress = pointerSincePress();
    if (uPointerPress.w > 0.5 && sincePress < duration
        && max(abs(px.x - uPointerPress.x), abs(px.y - uPointerPress.y)) <= spread) {
        float a = response(px, uPointerPress.xy, sincePress, duration, spread, half_);
        vec4 col = buttonColour(uPointerPress.w);
        a *= col.a;
        rgb += col.rgb * a;
        alpha += a;
    }

    // The release leaves a smaller second set. `releaseMark` at 0 turns it off.
    float echo = clamp(p_releaseMark, 0.0, 1.0);
    float sinceRelease = pointerSinceRelease();
    if (echo > 0.0 && uPointerRelease.w > 0.5 && sinceRelease < duration
        && max(abs(px.x - uPointerRelease.x), abs(px.y - uPointerRelease.y)) <= spread) {
        float a = response(px, uPointerRelease.xy, sinceRelease, duration, spread * 0.6, half_ * 0.7);
        vec4 col = buttonColour(uPointerRelease.w);
        a *= col.a * echo;
        rgb += col.rgb * a;
        alpha += a;
    }

    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    float clamped = min(alpha, 1.0);
    return vec4(rgb * (clamped / alpha), clamped);
}
