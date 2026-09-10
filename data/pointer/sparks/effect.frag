// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Sparks pointer shader — small sparks shed from each trail sample in
// proportion to its speed. Sparks are seeded from the sample's canvas
// position (not its ring index, which shifts as new samples arrive) so a
// spark keeps its launch direction and speed from frame to frame while its
// sample ages. The newest sample is the exception the seeding has to work
// around: the sampler refreshes slot 0 IN PLACE for up to a whole interval so
// the head stays exactly on the pointer, so its position is the one key in
// the ring that moves. Seeding the head off it re-rolled every spark on it
// once per device pixel travelled. The head is therefore seeded from the
// anchored sample behind it (see the note at the seed), which keeps its
// sparks stable while the cluster still rides the live pointer position. Each spark follows a ballistic arc under
// `gravity`, shrinks and fades over `life`, and shifts colour from colorA to
// colorB. Coverage is accumulated additively then clamped. Everything is
// gone once a sample's age passes `life`, which stays within the metadata's
// trailSeconds.
//
// A press throws an extra ring of sparks from the press point, `clickBurst`
// of them, seeded from the press position so they hold their directions
// while they fly. They use the same `life`, `gravity` and colours as the
// path sparks, so the burst is the pack doing more of its own thing rather
// than a new shape, and it is gone on the same schedule. The burst sits
// outside the speed gate, so a click still answers when the pointer is
// sitting still.
//
// `activationSpeed` gates the whole pack once through the shared
// pointerActivationGate(), and `smoothing` goes through pointerSmoothedAt(),
// the same two the other trail packs use, so every pack in a chain gates on
// the same filtered figure and smooths identically. The
// per-sample speed still decides how MANY sparks a sample sheds, since that
// is a property of the path at that point. Both default to 0, which is the
// no-threshold, raw-path behaviour the pack shipped with.

#include <pointer_noise.glsl>

const int kMaxSparks = 48;
// Speed (logical px/s, scaled at use) at which a sample sheds its full
// budget. Deliberately low: the settings preview's simulated pointer peaks
// near 324 px/s.
const float kFullSpeed = 220.0;
// The metadata trailSeconds. `life` is this pack's trailWindowParam, so the
// host spaces the ring over it and inflates the damage rect for it; a value
// past this would walk samples the host has already stopped repainting.
const float kTrailSeconds = 1.0;

// One spark's premultiplied contribution at `rel` (the fragment relative to
// the launch point). `h` is the spark's hash (its .z sets the radius), the
// launch `angle` and `speed` are the caller's (the path and the click fan
// aim differently), and the spark has flown for `age` seconds of `life`
// under `gravity`. The body has compact support at three radii, the extent
// the reach budget reserves, so a spark reaches zero where the box ends
// rather than being cut there at a faint level. Shared by the path sparks
// and the click burst so the two cannot drift apart in shape.
vec4 sparkAt(vec2 rel, vec3 h, float angle, float speed, float age, float life, float gravity, float size) {
    vec2 pos = vec2(cos(angle), sin(angle)) * speed * age + vec2(0.0, 0.5 * gravity * age * age);
    float lifeT = age / life;
    float remain = 1.0 - lifeT;
    float r = size * (0.5 + 0.5 * h.z) * (0.4 + 0.6 * remain);
    float d = length(rel - pos);
    float body = exp(-(d * d) / (2.0 * r * r)) * (1.0 - smoothstep(2.0 * r, 3.0 * r, d));
    float a = body * remain * remain * mix(p_colorA.a, p_colorB.a, lifeT);
    return vec4(mix(p_colorA.rgb, p_colorB.rgb, lifeT) * a, a);
}

// How far a spark launched `age` seconds ago can have travelled: launch
// plus the fall, plus the three radii a spark's compact window extends to.
// Evaluated PER SAMPLE from its own age for the reject box, since a young
// sample's sparks are still close to it and only the oldest reach the
// full-life envelope; a single full-life box ran every live sample's inner
// loop for every fragment within that envelope of it.
float sparkExtent(float age, float launch, float gravity, float size) {
    return launch * age + 0.5 * gravity * age * age + size * 3.0;
}

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 1) {
        return vec4(0.0);
    }

    // One gate for the path sparks, from the filtered speed: the raw
    // per-sample figure reads 0 whenever two events share a millisecond,
    // which gated per sample would blink patches of sparks. The click burst
    // below sits outside it.
    float gate = pointerActivationGate(p_activationSpeed);

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float life = clamp(p_life, 0.05, kTrailSeconds);
    float size = max(p_size, 0.25) * scale;
    float gravity = p_gravity * scale;
    float launch = 220.0 * max(p_spread, 0.0) * scale;
    float budget = clamp(p_count, 1.0, float(kMaxSparks));

    // Ballistic envelope, then the shrink that keeps it inside Reach, exactly
    // the way Burst does it — including giving the user the same escape valve.
    //
    // Left unshrunk, a spark's launch plus its fall runs well past the reach
    // the metadata declares. The host sizes the damage rect from that number
    // and nothing is painted outside it, so the far part of every spark's
    // flight was cut off on a straight edge instead of fading. Shrinking speed
    // and gravity together keeps the arc's shape and lands it on the budget.
    //
    // Reach is a PARAMETER rather than a constant for the reason Burst's is: a
    // fixed budget makes Spread and Gravity stop changing how far a spark
    // actually gets once the shrink is biting, which is most of their range.
    // With Reach in hand the user raises the budget instead of wondering why
    // the sliders stopped working. Read from the uniform the host filled from
    // that parameter, so the damage rect and the shader cannot drift.
    float reachPx = pointerReach();
    float travel = launch * life + 0.5 * gravity * life * life;
    float envelope = max(reachPx - size * 3.0, 0.0);
    float shrink = (travel > envelope && travel > 0.0) ? (envelope / travel) : 1.0;
    launch *= shrink;
    gravity *= shrink;

    // Only the LIVE run of samples sheds or is smoothed over, like the other
    // trail packs: the ring is never purged, and at a life equal to the
    // metadata trailSeconds the samples behind the run are outside the
    // damage rect, where the smoothing kernel would otherwise pull a launch
    // point toward them. The live count is the window pointerSmoothedAt
    // clamps its neighbours into.
    int live = pointerLiveCount(count, life);

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;
    for (int i = 0; i < kPointerTrailCapacity; ++i) {
        if (i >= live || gate <= 0.0) {
            break;
        }
        vec4 s = pointerTrailAt(i);
        // From index 8 on (the ninth sample), only half the samples shed.
        // The inner loop below is the pack's whole cost, so thinning the
        // older part of the tail nearly halves the worst case. The half is
        // chosen by a hash of the sample's POSITION, the same key its sparks
        // are seeded from: a sample's ring index advances by one on every
        // append, so choosing by index parity drew and skipped the same
        // sparks on alternate appends and the outer tail strobed at the
        // sample rate. A thinned sample's sparks fade out in BRIGHTNESS over
        // indices 5 to 8 rather than being cut at 8: fading the budget
        // instead dropped whole groups of sparks at each append, since a
        // spark past the budget goes from full share to none.
        //
        // The seed cell is one device pixel: with a coarser cell two slow
        // consecutive samples landed in the same cell, rolled identical
        // sparks and read as beads along one arc.
        //
        // The HEAD is seeded from the sample behind it instead of from its
        // own position, because its position is the one thing in the ring
        // that moves: the sampler refreshes slot 0 in place for up to a whole
        // interval so the head stays exactly on the pointer, and seeding off
        // that re-rolled every spark on it once per device pixel travelled —
        // tens of times per interval at speed. Slot 1 is anchored and does
        // not move, so the head's sparks keep their launch direction while
        // the cluster still rides the live pointer position through `s.xy`.
        // The offset keeps them from rolling identically to slot 1's own.
        // A one-sample ring has nothing behind it, but it is also at most one
        // interval old, so its own position will do.
        vec2 seed = i == 0 && live >= 2 ? floor(pointerTrailAt(1).xy) + 19.0 : floor(s.xy);
        float thin = hash13(seed + 57.0) < 0.5 ? 1.0 - smoothstep(5.0, 8.0, float(i)) : 1.0;
        if (thin <= 0.0) {
            continue;
        }
        // Spark budget for this sample scales with its speed. The shed
        // decision comes before the distance test so a sample that sheds
        // nothing costs nothing, whichever order the two would have rejected.
        float shed = budget * clamp(s.w / (kFullSpeed * scale), 0.0, 1.0) * gate;
        int sparks = int(ceil(shed));
        if (sparks < 1) {
            continue;
        }
        // Sparks launch from the smoothed path, but keep their seed on the
        // raw sample position: the seed has to stay put from frame to frame
        // or a spark would be re-rolled into a new direction as the smoothing
        // window slides over it.
        vec2 origin = pointerSmoothedAt(i, live, p_smoothing);
        vec2 rel = px - origin;
        float extent = sparkExtent(s.z, launch, gravity, size);
        if (abs(rel.x) > extent || abs(rel.y) > extent) {
            continue;
        }
        for (int k = 0; k < kMaxSparks; ++k) {
            if (k >= sparks) {
                break;
            }
            vec3 h = hash23(seed + vec2(float(k) * 7.31, float(k) * 3.17));
            // Fractional budget: the last spark is dimmer instead of popping.
            float share = clamp(shed - float(k), 0.0, 1.0);
            vec4 spark = sparkAt(rel, h, h.x * TAU, launch * (0.35 + 0.65 * h.y), s.z, life, gravity, size);
            rgb += spark.rgb * share * thin;
            alpha += spark.a * share * thin;
        }
    }

    float sincePress = pointerSincePress();
    int burst = clamp(int(p_clickBurst + 0.5), 0, kMaxSparks);
    if (burst > 0 && uPointerPress.w > 0.5 && sincePress < life) {
        vec2 origin = uPointerPress.xy;
        vec2 rel = px - origin;
        float extent = sparkExtent(sincePress, launch, gravity, size);
        if (abs(rel.x) <= extent && abs(rel.y) <= extent) {
            vec2 seed = floor(origin * 0.5) + 91.0;
            for (int k = 0; k < kMaxSparks; ++k) {
                if (k >= burst) {
                    break;
                }
                vec3 h = hash23(seed + vec2(float(k) * 5.77, float(k) * 2.19));
                // Even fan around the press point, jittered so it does not
                // read as a ring: that shape belongs to Click Ripple.
                float angle = (float(k) + 0.35 * h.x) / float(burst) * TAU;
                vec4 spark = sparkAt(rel, h, angle, launch * 0.75 * (0.4 + 0.6 * h.y), sincePress, life, gravity, size);
                rgb += spark.rgb;
                alpha += spark.a;
            }
        }
    }
    return premulAccumulated(rgb, alpha);
}
