// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Strip Chromatic — velocity-driven chromatic aberration on the scrolling
// strip. Red and blue sample apart along the motion axis, green stays put,
// so edges fringe cyan/orange while scrolling and reconverge at settle.
//
// Strip contract (strip_transition.glsl): intensity keys off iStripMotion,
// which converges to zero at settle, so the settle frame is the identity
// image.
#include <strip_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
    float m = stripMask(uv, p_edgeFeather);
    // Signed velocity in output-widths per second, saturating so a violent
    // fling cannot pull the channels apart by more than ~1.4% of the output.
    float shift = clamp(iStripMotion.w * 0.02 * p_strength, -0.014, 0.014) * m;
    // Die out before the shifted channels can sample past the screen edge,
    // where the clamped capture would smear (see stripEdgeFade).
    shift *= stripEdgeFade(uv, 0.03);
    if (abs(shift) < 1.0e-5) {
        return getStripColor(uv);
    }
    vec4 base = getStripColor(uv);
    float r = getStripColor(uv + vec2(shift, 0.0)).r;
    float b = getStripColor(uv - vec2(shift, 0.0)).b;
    // Taking r and b from neighbours while alpha comes from the centre can in
    // principle break the premultiplied invariant (rgb <= a). It cannot today:
    // the strip capture is the composited scene, opaque everywhere the strip
    // covers, so base.a is 1 wherever shift is non-zero. A future capture that
    // carries real alpha would need min(rgb, a) here.
    return vec4(r, base.g, b, base.a);
}
