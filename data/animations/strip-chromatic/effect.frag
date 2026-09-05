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
    float m = stripMask(uv, clamp(p_edgeFeather, 0.0, 0.2));
    // Signed velocity in output-extents along the travel axis per second,
    // saturating so a violent
    // fling cannot pull the channels apart by more than ~1.4% of the output.
    float shift = clamp(iStripMotion.w * 0.02 * p_strength, -0.014, 0.014) * m;
    // Die out before the shifted channels can sample past the screen edge,
    // where the clamped capture would smear (see stripEdgeFade).
    shift *= stripEdgeFade(uv, 0.03);
    if (abs(shift) < 1.0e-5) {
        return getStripColor(uv);
    }
    vec4 base = getStripColor(uv);
    vec4 rTap = getStripColor(uv + stripAxisOffset(shift));
    vec4 bTap = getStripColor(uv - stripAxisOffset(shift));
    // The capture carries real alpha (strip_transition.glsl): the gaps between
    // columns are transparent. Taking r and b from neighbours while alpha
    // comes from the centre alone would break the premultiplied invariant
    // (rgb <= a) at every column edge and paint a red or blue fringe with no
    // coverage behind it. Coverage is the UNION of the three taps instead:
    // each channel came from a premultiplied sample whose alpha is at most
    // this one, so the invariant holds, and the fringe that spills past a
    // column edge is drawn with the coverage of the tap it came from — which
    // is what chromatic aberration at a window edge looks like. No clamp on
    // rgb, so HDR captures keep their headroom.
    float a = max(base.a, max(rTap.a, bTap.a));
    return vec4(rTap.r, base.g, bTap.b, a);
}
