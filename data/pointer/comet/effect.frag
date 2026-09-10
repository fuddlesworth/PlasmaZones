// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Comet pointer shader — one bright head on the newest trail sample and a
// tapered single-colour tail along the trail polyline, with hashed grain
// twinkling through the tail. Coverage is the max over segments. The head
// itself fades with idle time so the whole comet is gone within the
// metadata's trailSeconds even while the pointer rests.
//
// A press makes the head flare and throws an extra helping of the same
// sparkle grain out from the press point, scaled by `clickBurst`. The burst
// is kept inside `reach` of the press point so it stays within the damage
// rect the host derives from that parameter, and it is gone by
// kBurstSeconds, well inside trailSeconds.
//
// REACH. `width` is a stroke thickness and says nothing about how far the
// glow around it extends, so the pack declares a separate `reach` parameter,
// the one its metadata reachParam names. Every gaussian here has compact
// support that ends exactly there: the head glow, the tail's soft term and
// the burst all multiply by a window that is 1 inside 80% of the reach and
// 0 at it, so nothing meets the damage rect's edge at a visible level.

#include <pointer_noise.glsl>

const float kTrailSeconds = 0.8;
const float kBurstSeconds = 0.35;

// 1 well inside the reach, exactly 0 at it.
float cometWindow(float d, float reach) {
    return 1.0 - smoothstep(0.8 * reach, reach, d);
}

vec4 pPointer(vec2 uv) {
    // No early return on an empty trail: a click before any motion this
    // session still bursts (the burst answers a resting pointer by design),
    // and every trail read below is guarded on `count`.
    int count = pointerTrailCount();

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    // Capped at 0.9 of the window (the metadata maximum is that cap) for the
    // reason the head fade ends there: the last live frame at a low refresh
    // rate lands just inside the window, and a tail still fading at the edge
    // would leave its last sliver frozen at 20 Hz and below.
    float tailSeconds = clamp(p_length, 0.05, 0.9 * kTrailSeconds);
    // The reach in device px: both the reject box and the outer edge of every
    // glow, so the two cannot disagree. Read from the uniform the host filled
    // from the parameter the metadata's reachParam names, so the damage rect
    // and the shader can never drift apart.
    float reach = pointerReach();
    // The solid head disc and tail body carry a 0.75 px feather past their
    // radius that the gaussian windows do not cover, so the radius is held
    // that far inside the reach: at the widest stroke on the smallest reach
    // the core would otherwise poke past the damage rect and be cut flat.
    float radius = min(0.5 * max(p_width, 0.5) * scale, reach - 0.75);
    // Grain cell, in device px. Well above a pixel so it reads as twinkling
    // flecks rather than per-pixel dither.
    float grainCell = max(4.0 * scale, 1.0);

    // One gate for the whole comet, from the filtered speed: the raw per-event
    // velocity reads 0 whenever two events share a millisecond, which would
    // blink the head and the newest tail segment on and off.
    float gate = pointerActivationGate(p_activationSpeed);

    // Head: a soft dot on the newest sample that dims as the pointer idles,
    // and is fully out by the end of the live window. It sits on the smoothed
    // path like the tail does, and the speed gate applies to it too, so below
    // the activation speed the whole comet is absent rather than leaving a
    // headlight behind.
    vec2 headPos = pointerSmoothedAt(0, count, p_smoothing);
    float idle = pointerIdleSeconds();
    // Out at 0.9 of the window rather than exactly at it: liveness is a
    // strict `<`, so the last painted frame at a low refresh rate lands just
    // inside the window and nothing repaints after it. Ending the fade
    // early keeps that frame clear instead of leaving a faint dot frozen at
    // the head (Halo and Afterglow keep a similar margin). Nothing with no
    // trail: the zero entry would put a head at the canvas origin, so both
    // head terms are gated here.
    float idleFade = 1.0 - smoothstep(0.35 * kTrailSeconds, 0.9 * kTrailSeconds, idle);
    float headFade = count >= 1 ? idleFade * gate : 0.0;
    float dHead = length(px - headPos);
    // The head's disc, shared with the click lift below so the two cannot
    // disagree about where the head ends.
    float headDisc = count >= 1 ? 1.0 - smoothstep(radius - 0.75, radius + 0.75, dHead) : 0.0;
    float headCore = headDisc * headFade;
    float headGlow =
        exp(-(dHead * dHead) / (2.0 * radius * radius * 2.25)) * 0.5 * headFade * cometWindow(dHead, reach);

    float tail = 0.0;
    float tailGrain = 0.0;
    // The smoothed far end of one segment is the near end of the next, so it
    // is carried across iterations rather than looked up twice per segment.
    vec2 pa = headPos;
    for (int i = 0; i < kPointerTrailCapacity - 1; ++i) {
        if (i + 1 >= count) {
            break;
        }
        vec4 a = pointerTrailAt(i);
        vec4 b = pointerTrailAt(i + 1);
        if (a.z >= tailSeconds) {
            break;
        }
        vec2 pb = pointerSmoothedAt(i + 1, count, p_smoothing);
        vec2 lo = min(pa, pb) - reach;
        vec2 hi = max(pa, pb) + reach;
        if (px.x < lo.x || px.y < lo.y || px.x > hi.x || px.y > hi.y) {
            pa = pb;
            continue;
        }

        float t;
        float d = pointerSegmentDistanceFrom(px, pa, pb, t);
        pa = pb;
        float age = clamp(mix(a.z, b.z, t) / tailSeconds, 0.0, 1.0);
        float life = (1.0 - age) * gate;
        float w = radius * life;
        float body = (1.0 - smoothstep(w - 0.75, w + 0.75, d)) * life * life;
        float soft = exp(-(d * d) / (2.0 * (w + scale) * (w + scale) * 4.0)) * 0.3 * life * life
            * cometWindow(d, reach);
        float here = max(body, soft);
        if (here > tail) {
            tail = here;
            // Grain that lives in the tail's own frame: the cell is addressed
            // by the segment index and the position along it, so the flecks
            // ride with the stroke instead of sitting on the screen like
            // dither. A slow time step re-rolls them so they twinkle rather
            // than crawl, with two independent offsets so the re-roll is not
            // a diagonal walk that repeats after a few steps. `tick`, not
            // `step`, which is a GLSL builtin.
            float tick = floor(iTime * 12.0);
            vec2 cell = floor(vec2(float(i) * 8.0 + t * 8.0, d / grainCell))
                + vec2(hash13(vec2(tick, 7.0)), hash13(vec2(tick, 19.0))) * 512.0;
            float g = hash13(cell);
            tailGrain = smoothstep(0.75, 1.0, g) * soft * 3.0;
        }
    }

    // The tail ends on the idle clock like the head, not only on sample age:
    // a host that appends a rest slot every interval keeps a sample at age
    // zero under a resting pointer, and its degenerate segment would hold a
    // full-width disc there after the head had faded.
    tail *= idleFade;
    tailGrain *= idleFade;

    // Click burst: the head flares and sheds a fistful of extra grain from
    // the press point. Everything is cut to zero at the reach so the burst
    // cannot paint outside the declared damage rect. It is deliberately
    // outside the speed gate, so a click still answers when the pointer is
    // sitting still.
    float burst = 0.0;
    float sincePress = pointerSincePress();
    if (p_clickBurst > 0.0 && uPointerPress.w > 0.5 && sincePress < kBurstSeconds) {
        float ct = sincePress / kBurstSeconds;
        float decay = (1.0 - ct) * (1.0 - ct);
        vec2 rel = px - uPointerPress.xy;
        float dp = length(rel);
        float ballSigma = mix(radius * 0.5, reach * 0.45, ct);
        float ball = exp(-(dp * dp) / (2.0 * ballSigma * ballSigma));
        // Burst grain is addressed in the press point's own frame, with the
        // same two-offset re-roll as the tail grain.
        float tick = floor(iTime * 20.0);
        vec2 cell = floor(rel / grainCell) + vec2(hash13(vec2(tick, 3.0)), hash13(vec2(tick, 11.0))) * 512.0;
        float g = hash13(cell);
        float grain = smoothstep(0.55, 1.0, g);
        float edge = cometWindow(dp, reach);
        burst = decay * edge * (ball * 0.5 + ball * grain * 1.5) * p_clickBurst;
        // The head itself lifts with the burst rather than only the grain.
        // Added, not scaled: a click on a resting pointer has headCore at 0
        // (its idle fade is out), and scaling nothing lifts nothing. The lift
        // is shaped by the head's own disc, not a flat constant: a constant
        // here paints every pixel of the pass quad and shows up as a filled
        // box the size of the damage rect on every click.
        headCore = min(headCore + decay * 0.4 * p_clickBurst * headDisc, 1.0);
    }

    float alpha = clamp(headCore + headGlow + tail + tailGrain * p_sparkle + burst, 0.0, 1.0);
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    return premul(p_color.rgb, alpha * p_color.a);
}
