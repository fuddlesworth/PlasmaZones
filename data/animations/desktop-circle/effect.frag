// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Circle — an aspect-correct iris reveal. By default a circle of the
// INCOMING desktop grows from the center point until it fills the screen; with
// "Close Instead of Open" the OUTGOING desktop shrinks into a closing circle
// instead. p_centerX / p_centerY place the center, p_softness sets the edge
// feather. `t` is forward switch progress in [0,1].
#include <desktop_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    vec2 center = vec2(p_centerX, p_centerY);
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    // Aspect-correct the distance so the iris is a round circle, not an oval.
    vec2 d = uv - center;
    d.x *= aspect;
    float dist = length(d);

    // Farthest corner from the center, so the circle fully covers the screen at
    // the end (or starts fully covering it, when closing).
    vec2 corner = max(center, 1.0 - center);
    float maxR = length(vec2(corner.x * aspect, corner.y));
    float soft = max(p_softness, 1.0e-3) * maxR;

    bool close = p_invert > 0.5;
    float progR = close ? (1.0 - t) : t;                  // open grows, close shrinks
    float radius = progR * (maxR + 2.0 * soft) - soft;    // padded so it clears at both ends
    float outsideMask = smoothstep(radius - soft, radius + soft, dist); // 0 inside, 1 outside

    vec4 inside = close ? getFromColor(uv) : getToColor(uv);
    vec4 outside = close ? getToColor(uv) : getFromColor(uv);
    return mix(inside, outside, outsideMask);
#else
    return vec4(0.0);
#endif
}
