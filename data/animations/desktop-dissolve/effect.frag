// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Dissolve — each speckle of the screen flips from the outgoing desktop
// to the incoming one at its own random threshold, so the switch dissolves
// through noise. p_scale sets the speckle density, p_softness the per-speckle
// fade width. `t` is forward switch progress in [0,1].
#include <desktop_transition.glsl>
// classicHash (from noise.glsl) and crossFade (from desktop_transition.glsl)
// replace this pack's per-copy pz_hash and inline getFrom/getTo mix.
#include <noise.glsl>

vec4 pTransition(vec2 uv, float t) {
    float n = classicHash(floor(uv * max(p_scale, 1.0)));
    float soft = max(p_softness, 1.0e-3);
    // Pad progress by the fade width so every speckle is fully outgoing at t=0
    // and fully incoming at t=1 — otherwise speckles whose threshold n lands
    // within `soft` of the ends show a partial blend and the switch pops. Same
    // padding the wipe and aretha packs use.
    float p = t * (1.0 + 2.0 * soft) - soft;
    float a = smoothstep(n - soft, n + soft, p);
    return crossFade(uv, a);
}
