// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: gl-transitions contributors
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Cross Zoom — the outgoing desktop zooms and directional-blurs toward a
// focal point while dissolving into the incoming one, for a cinematic switch.
// Ported from GL-Transitions "CrossZoom" by rectalogic
// (https://github.com/gl-transitions/gl-transitions, MIT). progress -> t; the
// blur loop variable is `i` (our progress is `t`). p_strength = zoom strength.
#include <desktop_transition.glsl>
// classicHash (from noise.glsl) and crossFade (from desktop_transition.glsl)
// replace this pack's per-copy pz_rand and pz_crossFade.
#include <noise.glsl>

const float PZ_PI = 3.141592653589793;

float pz_linearEase(float begin, float change, float duration, float time) {
    return change * time / duration + begin;
}

float pz_expEaseInOut(float begin, float change, float duration, float time) {
    if (time == 0.0) {
        return begin;
    } else if (time == duration) {
        return begin + change;
    }
    time = time / (duration / 2.0);
    if (time < 1.0) {
        return change / 2.0 * pow(2.0, 10.0 * (time - 1.0)) + begin;
    }
    return change / 2.0 * (-pow(2.0, -10.0 * (time - 1.0)) + 2.0) + begin;
}

float pz_sinEaseInOut(float begin, float change, float duration, float time) {
    return -change / 2.0 * (cos(PZ_PI * time / duration) - 1.0) + begin;
}

vec4 pTransition(vec2 uv, float t) {
    vec2 center = vec2(pz_linearEase(0.25, 0.5, 1.0, t), 0.5);
    float dissolve = pz_expEaseInOut(0.0, 1.0, 1.0, t);
    // Clamp: pz_sinEaseInOut is PERIODIC in t (it is a cosine), so while it lands
    // on 0 at t = 1.0 as intended, an overshoot to t = 1.2 swings it back up to
    // ~0.35 * p_strength and the zoom blur RE-BLOOMS after the transition should
    // have settled. (The dissolve above is safe: pz_expEaseInOut saturates.)
    float str = pz_sinEaseInOut(0.0, p_strength, 0.5, clamp(t, 0.0, 1.0));
    vec4 color = vec4(0.0);
    float total = 0.0;
    vec2 toCenter = center - uv;
    float offset = classicHash(uv);
    for (float i = 0.0; i <= 40.0; i++) {
        float percent = (i + offset) / 40.0;
        float weight = 4.0 * (percent - percent * percent);
        color += crossFade(uv + toCenter * percent * str, dissolve) * weight;
        total += weight;
    }
    return color / total;
}
