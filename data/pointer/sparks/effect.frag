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

#include <pointer_noise.glsl>

const int kMaxTrail = 32;
const int kMaxSparks = 48;

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
        vec2 rel = px - s.xy;
        if (abs(rel.x) > reach || abs(rel.y) > reach) {
            continue;
        }

        // Spark budget for this sample scales with its speed.
        float shed = budget * clamp(s.w / 1400.0, 0.0, 1.0);
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

    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    float clamped = min(alpha, 1.0);
    return vec4(rgb * (clamped / alpha), clamped);
}
