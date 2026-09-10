// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Burst pointer shader — a click-led pack. A press throws `count` small bits
// out of the press point on an even fan, jittered by a hash so the spray does
// not read as a regular star. Each bit keeps its own hashed direction, speed
// and radius, flies a parabola under `gravity` and fades as it falls. A
// release throws a smaller, shorter spray of the same bits when
// `releaseSpray` is above 0.
//
// MOTION DRAWS NOTHING. The body never reads uPointerTrail or uPointerVelocity,
// so a pointer that is merely moving produces transparent black everywhere and
// the host has nothing to composite.
//
// Every bit's hash is seeded from the EVENT POSITION and the bit index, never
// from iTime, so one click throws the same spray for the whole of its life
// instead of boiling from frame to frame.
//
// Coloured per button, following the Click Ripple convention. Layer "below",
// like every pack here except Click Ripple: the spray reads fine under the
// cursor, and staying below keeps the pointer on the hardware plane.
//
// REACH. A bit's displacement from its origin is |v|t + 0.5*g*t^2, which over
// a life L is at most speed*L + 0.5*gravity*L*L. `speed` and `gravity` are
// scaled down together by `k` so that bound, plus the bit's own drawn radius,
// stays inside `reach` — the parameter `reachParam` names, so the host's
// damage rect is exactly what the pack can paint. The travel budget is
// floored at 35 percent of the reach (see the budget line), so a large bit
// on a small reach may overrun the rect a little rather than never leaving
// the press point; the box clip still keeps every pixel inside it. Scaling
// both by the same k keeps the parabola's shape, it only shrinks it.

#include <pointer_noise.glsl>

const int kMaxBits = 48;

vec4 burstColour(float button) {
    return pointerButtonColour(button, p_colorLeft, p_colorRight, p_colorMiddle);
}

// A single bit's contribution. `rel` is the fragment relative to the event
// origin, `h` its hash, `age`/`life` its clock. Falloff has compact support:
// it is exactly zero at three radii, so the bit has a hard outer edge the
// reach bound can account for.
float bitCoverage(vec2 rel, vec3 h, int index, int total, float speed, float gravity, float age, float life,
                  float radius) {
    float t = age / life;
    float remain = 1.0 - t;
    // Even fan, jittered by up to most of one slot so the spray is irregular.
    float angle = (float(index) + 0.8 * (h.x - 0.5)) / float(total) * TAU;
    float v = speed * (0.45 + 0.55 * h.y);
    vec2 pos = vec2(cos(angle), sin(angle)) * v * age + vec2(0.0, 0.5 * gravity * age * age);
    float r = radius * (0.5 + 0.5 * h.z) * (0.45 + 0.55 * remain);
    float d = length(rel - pos);
    float span = 3.0 * r;
    if (d >= span) {
        return 0.0;
    }
    float q = 1.0 - (d * d) / (span * span);
    return q * q * remain * remain;
}

vec4 pPointer(vec2 uv) {
    int count = clamp(int(p_count + 0.5), 0, kMaxBits);
    if (count < 1) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float life = clamp(p_life, 0.15, 0.9);
    float radius = max(p_size, 0.5) * scale;
    // The reach the host resolved from the `reach` parameter, in device px,
    // read from the uniform so the damage rect and the shader cannot drift.
    float reach = pointerReach();

    // Ballistic envelope, then the shrink factor that keeps it inside reach.
    float speed = max(p_speed, 0.0) * scale;
    float gravity = max(p_gravity, 0.0) * scale;
    float travel = speed * life + 0.5 * gravity * life * life;
    // The bit's own drawn extent comes out of the budget, floored at a share
    // of the reach: at the smallest reach and the largest bit the bare
    // subtraction left two pixels of travel, which froze the spray on the
    // press point. A big bit on a small reach now overruns the rect a little
    // instead of never leaving.
    float budget = max(reach - 3.0 * radius, reach * 0.35);
    float k = (travel > budget && travel > 0.0) ? (budget / travel) : 1.0;
    speed *= k;
    gravity *= k;

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;

    float sincePress = pointerSincePress();
    if (uPointerPress.w > 0.5 && sincePress < life) {
        vec2 rel = px - uPointerPress.xy;
        if (abs(rel.x) <= reach && abs(rel.y) <= reach) {
            vec2 seed = floor(uPointerPress.xy * 0.5);
            vec4 c = burstColour(uPointerPress.w);
            for (int i = 0; i < kMaxBits; ++i) {
                if (i >= count) {
                    break;
                }
                vec3 h = hash23(seed + vec2(float(i) * 5.77, float(i) * 2.19));
                float a = bitCoverage(rel, h, i, count, speed, gravity, sincePress, life, radius) * c.a;
                rgb += c.rgb * a;
                alpha += a;
            }
        }
    }

    // The release spray: fewer bits, six tenths of the life, slower off the
    // mark, so it reads as an echo of the press rather than a second event.
    float release = clamp(p_releaseSpray, 0.0, 1.0);
    float releaseLife = life * 0.6;
    int releaseCount = int(float(count) * 0.5 * release + 0.5);
    float sinceRelease = pointerSinceRelease();
    if (releaseCount > 0 && uPointerRelease.w > 0.5 && sinceRelease < releaseLife) {
        vec2 rel = px - uPointerRelease.xy;
        if (abs(rel.x) <= reach && abs(rel.y) <= reach) {
            vec2 seed = floor(uPointerRelease.xy * 0.5) + 131.0;
            vec4 c = burstColour(uPointerRelease.w);
            for (int i = 0; i < kMaxBits; ++i) {
                if (i >= releaseCount) {
                    break;
                }
                vec3 h = hash23(seed + vec2(float(i) * 3.91, float(i) * 6.13));
                float a = bitCoverage(rel, h, i, releaseCount, speed * release, gravity, sinceRelease, releaseLife,
                                      radius * 0.8)
                    * c.a * 0.8;
                rgb += c.rgb * a;
                alpha += a;
            }
        }
    }

    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    // Overlapping bits accumulate; clamp coverage and keep the colour mix in
    // proportion so the premultiplied result stays consistent.
    float clamped = min(alpha, 1.0);
    return vec4(rgb * (clamped / alpha), clamped);
}
