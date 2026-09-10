// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// WindTrail pointer shader — a continuous curved ribbon whose width and
// lifetime both answer to how fast the pointer is travelling. Original GLSL.
// This reimplements the behaviour of the wesleyyach/windtrail KWin effect,
// which is C++ and builds its ribbon from quads, so there is no shader to
// port. What is reproduced here is that effect's SHAPE, taken from its
// rendering code rather than from its description:
//
//   • Speed is filtered before anything is decided by it. Upstream keeps one
//     exponentially filtered speed (a = 0.46) rather than reading the raw
//     per-frame figure. pointerFilteredSpeed() is that filter. Gating on the
//     raw per-sample speed is what made this pack blink on and off.
//   • Width answers to the SQUARE ROOT of speed, which compresses the top of
//     the range so a fast flick is not absurdly fatter than a brisk drag.
//   • Width also tapers with age on a `pow(smoothstep(life), 1.55)` curve, so
//     the ribbon narrows toward its old end. Upstream tapers hard. This is a
//     wide ribbon that narrows, not comet's narrow tail behind a bright head.
//   • Lifetime is floored at 53% of `duration`, upstream's clamp. An earlier
//     revision here dropped to 15%, which made slow strokes vanish almost
//     before they were drawn and read as flicker.
//   • The ribbon fades out as the pointer comes to rest rather than being cut
//     off, upstream's stop fade.
//
// Smoothing goes through the shared pointerSmoothedAt(). Upstream uses Chaikin
// corner cutting; a weighted neighbour blend is one pass of the same idea, and
// the shared helper is mandatory so two packs in a chain cannot trace
// different curves from the same pointer.
//
// Coverage is the max over segments so a folded path does not stack.

// Speed at which the ribbon reaches full width, in px/s. The sqrt response is
// normalised against this so `thickness` means the same width on any machine.
const float kFullWidthSpeed = 900.0;

// Stop fade, as a share of `duration`: the seconds of stillness over which
// the ribbon fades away once the pointer stops. Scaled off the duration
// rather than fixed, because a fixed 0.35 s took the whole ribbon to zero
// long before a 1.2 s duration had let its far end age out, which made most
// of the Duration slider inert once the hand stopped. The ratio reproduces
// the old 0.35 s at the default 0.5 s duration, and the result is floored so
// a very short duration still gets a fade rather than a cut, and capped at
// the duration itself so it stays inside trailSeconds.
const float kStopFadeShare = 0.35 / 0.5;
const float kStopFadeFloorSeconds = 0.2;

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 2) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float duration = max(p_duration, 0.05);
    float halfWidth = 0.5 * max(p_thickness, 0.5) * scale;

    // One gate for the whole ribbon, from the FILTERED speed. Upstream gates
    // whether a trail starts at all, not each segment every frame, so the
    // ribbon appears and retreats as a whole instead of breaking into
    // flickering patches wherever the raw speed dipped for one sample.
    float gate = pointerSpeedGate(pointerFilteredSpeed(), p_activationSpeed);
    if (gate <= 0.0) {
        return vec4(0.0);
    }

    // Stop fade: rest dims the whole ribbon out rather than cutting it.
    float stopFadeSeconds = clamp(kStopFadeShare * duration, kStopFadeFloorSeconds, duration);
    float stopFade = 1.0 - smoothstep(0.0, stopFadeSeconds, pointerIdleSeconds());
    if (stopFade <= 0.0) {
        return vec4(0.0);
    }

    // The reach the host resolved, in device px. It is both the per-segment
    // reject box and the outer edge of the soft glow below, so the glow never
    // meets the damage rect's edge at a visible level.
    float reach = pointerReach();

    float cover = 0.0;
    // The smoothed far end of one segment is the near end of the next, so it
    // is carried across iterations rather than looked up twice per segment.
    vec2 pa = pointerSmoothedAt(0, count, p_smoothing);
    for (int i = 0; i < kPointerTrailCapacity - 1; ++i) {
        if (i + 1 >= count) {
            break;
        }
        vec4 a = pointerTrailAt(i);
        vec4 b = pointerTrailAt(i + 1);
        if (a.z >= duration) {
            break;
        }

        // Reject box from the RAW samples i-1..i+2, before the smoothing
        // reads: a smoothed sample is a convex blend of its raw neighbours
        // (clamped into the filled window, as pointerSmoothedAt clamps them),
        // so the smoothed segment lies inside the box of those four, inflated
        // by the reach. Exact, never clips, and a rejected fragment skips the
        // smoothing lookup as well as the distance maths.
        vec2 rawPrev = pointerTrailAt(max(i - 1, 0)).xy;
        vec2 rawNext = pointerTrailAt(min(i + 2, count - 1)).xy;
        vec2 rawLo = min(min(a.xy, b.xy), min(rawPrev, rawNext)) - reach;
        vec2 rawHi = max(max(a.xy, b.xy), max(rawPrev, rawNext)) + reach;
        if (any(lessThan(px, rawLo)) || any(greaterThan(px, rawHi))) {
            pa = pointerSmoothedAt(i + 1, count, p_smoothing);
            continue;
        }
        vec2 pb = pointerSmoothedAt(i + 1, count, p_smoothing);
        if (distance(pa, pb) < 1e-4) {
            // A stationary pair has no ribbon to draw. The preview appends
            // one every interval while its pointer rests, and drawing those
            // would keep the rest point lit there when the compositor, which
            // gets no event from a resting pointer, lets it age out.
            pa = pb;
            continue;
        }
        float t;
        float d = pointerSegmentDistanceFrom(px, pa, pb, t);
        pa = pb;

        // Per-sample speed, normalised and square-rooted. This one stays raw
        // because it describes how fast the pointer was AT this point of the
        // path, which is a property of the path and not of the frame clock.
        float segSpeed = mix(a.w, b.w, t);
        float speedNorm = clamp(segSpeed / kFullWidthSpeed, 0.0, 1.0);
        float responsive = sqrt(speedNorm);

        // Lifetime answers to speed, floored at 53% of duration as upstream
        // clamps it, so a slow stretch still lives long enough to be seen.
        float life = duration * mix(0.53, 1.0, responsive);
        float age = mix(a.z, b.z, t);
        if (age >= life) {
            continue;
        }
        float remain = 1.0 - age / life;

        // Upstream's taper: smoothstep on remaining life, raised to 1.55.
        float tail = pow(smoothstep(0.0, 1.0, remain), 1.55);
        // Upstream's width shape, 0.18 of a floor plus a speed-driven bulk,
        // both scaled by the taper.
        float w = halfWidth * (0.18 + 0.82 * responsive) * tail;
        w = max(w, 0.35 * scale);

        float core = 1.0 - smoothstep(w - 0.75, w + 0.75, d);
        float sigma = w + 1.5 * scale;
        // Compact support: the gaussian alone is still visible at the reject
        // box, so it is windowed to reach exactly zero at the reach.
        float soft = exp(-(d * d) / (2.0 * sigma * sigma)) * 0.4 * (1.0 - smoothstep(0.8 * reach, reach, d));
        // `tail` is applied a second time here, on the opacity, after it
        // already shaped the width above. That is deliberate and not a
        // duplicate: the width has a floor (0.35 px) it can never taper
        // below, so without this end fade the ribbon's old end would stop at
        // a hairline and then vanish in one frame instead of dissolving.
        cover = max(cover, max(core, soft) * tail);
    }

    float alpha = clamp(cover * gate * stopFade * max(p_intensity, 0.0) * p_color.a, 0.0, 1.0);
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    return premul(p_color.rgb, alpha);
}
