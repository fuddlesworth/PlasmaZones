// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Sparks pointer shader — small sparks shed from each trail sample in
// proportion to its speed. Sparks are seeded from the sample's canvas
// position (not its ring index, which shifts as new samples arrive) so a
// spark keeps its launch direction and speed from frame to frame while its
// sample ages. Each spark follows a ballistic arc under `gravity`, shrinks
// and fades over `life`, and shifts colour from colorA to colorB. Coverage
// is accumulated additively then clamped. Everything is gone once a
// sample's age passes `life`, which stays within the metadata's
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
// `activationSpeed` and `smoothing` come from the shared pointerSpeedGate()
// and pointerSmoothedAt(), the same two the other trail packs use, so every
// pack in a chain thresholds and smooths identically. Both default to 0,
// which is the no-threshold, raw-path behaviour the pack shipped with.

#include <pointer_noise.glsl>

const int kMaxTrail = 32;
const int kMaxSparks = 48;
// Mirrors "reach" in metadata.json, in logical px. The host derives the damage
// rect from that number and the shader has no uniform for it, so the two are
// kept in step by hand: change one and change this.
const float kReachPx = 96.0;

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 1) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float life = max(p_life, 0.05);
    float size = max(p_size, 0.25) * scale;
    float gravity = p_gravity * scale;
    float launch = 220.0 * max(p_spread, 0.0) * scale;
    float budget = clamp(p_count, 1.0, float(kMaxSparks));

    // Ballistic envelope, then the shrink that keeps it inside the declared
    // reach, the same way Burst does it.
    //
    // Left unshrunk, a spark's launch plus its fall runs well past `reach` in
    // the metadata at the shipped defaults, and further still at the maxima.
    // The host sizes the damage rect from that number and nothing is painted
    // outside it, so the far half of every spark's flight was being cut off on
    // a straight edge rather than fading. Compressing the flight into the
    // budget costs no visible sparks — those were already clipped away — and
    // loses the rectangle.
    float travel = launch * life + 0.5 * gravity * life * life;
    float envelope = max(kReachPx * scale - size * 3.0, 0.0);
    float k = (travel > envelope && travel > 0.0) ? (envelope / travel) : 1.0;
    launch *= k;
    gravity *= k;

    // Furthest a spark can travel in its life: launch plus the fall.
    float reach = launch * life + 0.5 * gravity * life * life + size * 3.0;

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;
    for (int i = 0; i < kMaxTrail; ++i) {
        if (i >= count) {
            break;
        }
        vec4 s = pointerTrailAt(i);
        if (s.z >= life) {
            break;
        }
        // Below the activation speed this sample sheds nothing at all.
        float gate = pointerSpeedGate(s.w, p_activationSpeed);
        if (gate <= 0.0) {
            continue;
        }
        // Sparks launch from the smoothed path, but keep their seed on the
        // raw sample position: the seed has to stay put from frame to frame
        // or a spark would be re-rolled into a new direction as the smoothing
        // window slides over it.
        vec2 origin = pointerSmoothedAt(i, count, p_smoothing);
        vec2 rel = px - origin;
        if (abs(rel.x) > reach || abs(rel.y) > reach) {
            continue;
        }

        // Spark budget for this sample scales with its speed.
        float shed = budget * clamp(s.w / 1400.0, 0.0, 1.0) * gate;
        int sparks = int(ceil(shed));
        if (sparks < 1) {
            continue;
        }
        vec2 seed = floor(s.xy * 0.5);
        for (int k = 0; k < kMaxSparks; ++k) {
            if (k >= sparks) {
                break;
            }
            vec3 h = hash23(seed + vec2(float(k) * 7.31, float(k) * 3.17));
            // Fractional budget: the last spark is dimmer instead of popping.
            float share = clamp(shed - float(k), 0.0, 1.0);
            float angle = h.x * TAU;
            float speed = launch * (0.35 + 0.65 * h.y);
            float age = s.z;
            vec2 pos = vec2(cos(angle), sin(angle)) * speed * age + vec2(0.0, 0.5 * gravity * age * age);
            float lifeT = age / life;
            float remain = 1.0 - lifeT;
            float r = size * (0.5 + 0.5 * h.z) * (0.4 + 0.6 * remain);
            float d = length(rel - pos);
            float body = exp(-(d * d) / (2.0 * r * r));
            float fade = remain * remain * share;
            vec3 c = mix(p_colorA.rgb, p_colorB.rgb, lifeT);
            float ca = mix(p_colorA.a, p_colorB.a, lifeT);
            float a = body * fade * ca;
            rgb += c * a;
            alpha += a;
        }
    }

    float sincePress = pointerSincePress();
    int burst = clamp(int(p_clickBurst + 0.5), 0, kMaxSparks);
    if (burst > 0 && uPointerPress.w > 0.5 && sincePress < life) {
        vec2 origin = uPointerPress.xy;
        vec2 rel = px - origin;
        if (abs(rel.x) <= reach && abs(rel.y) <= reach) {
            vec2 seed = floor(origin * 0.5) + 91.0;
            float age = sincePress;
            float lifeT = age / life;
            float remain = 1.0 - lifeT;
            for (int k = 0; k < kMaxSparks; ++k) {
                if (k >= burst) {
                    break;
                }
                vec3 h = hash23(seed + vec2(float(k) * 5.77, float(k) * 2.19));
                // Even fan around the press point, jittered so it does not
                // read as a ring: that shape belongs to Click Ripple.
                float angle = (float(k) + 0.35 * h.x) / float(burst) * TAU;
                float speed = launch * 0.75 * (0.4 + 0.6 * h.y);
                vec2 pos = vec2(cos(angle), sin(angle)) * speed * age + vec2(0.0, 0.5 * gravity * age * age);
                float r = size * (0.5 + 0.5 * h.z) * (0.4 + 0.6 * remain);
                float d = length(rel - pos);
                float body = exp(-(d * d) / (2.0 * r * r));
                float fade = remain * remain;
                vec3 c = mix(p_colorA.rgb, p_colorB.rgb, lifeT);
                float ca = mix(p_colorA.a, p_colorB.a, lifeT);
                float a = body * fade * ca;
                rgb += c * a;
                alpha += a;
            }
        }
    }

    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    float clamped = min(alpha, 1.0);
    return vec4(rgb * (clamped / alpha), clamped);
}
