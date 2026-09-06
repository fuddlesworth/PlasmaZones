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
    // Fetched once, above the early-out: both arms need it, and the branch
    // is per fragment (shift depends on the mask and the edge fade), so a
    // fetch in each arm would be two live fetches in the same draw.
    vec4 base = getStripColor(uv);
    float m = stripMask(uv, clamp(p_edgeFeather, 0.0, 0.2));
    // Signed velocity in output-extents along the travel axis per second,
    // saturating so a violent fling cannot displace red or blue more than
    // ~1.4% of the output from green (2.8% between them).
    float shift = clamp(iStripMotion.w * 0.02 * p_strength, -0.014, 0.014) * m;
    // Die out before the shifted channels can sample past the screen edge,
    // where the clamped capture would smear (see stripEdgeFade).
    shift *= stripEdgeFade(uv, 0.03);
    if (abs(shift) < 1.0e-5) {
        return base;
    }
    vec4 rTap = getStripColor(uv + stripAxisOffset(shift));
    vec4 bTap = getStripColor(uv - stripAxisOffset(shift));
    // The capture carries real alpha (strip_transition.glsl): the gaps
    // between columns are transparent, and the entry point composites this
    // result over the below-strip content with ONE alpha. Red and blue come
    // from taps whose coverage differs from the centre's, so alpha is the
    // union of the three taps and each channel is compensated by the
    // backdrop times its own coverage deficit: after the entry point's
    // composite, out = rgb + below * (1 - a), every channel equals
    // tap.c + below.c * (1 - tap.a), the exact result of compositing that
    // channel's own tap over the backdrop. A column edge fringes over the
    // wallpaper in the gap, and the wallpaper shows through the column where
    // a channel has moved away, which is what chromatic aberration at a
    // window edge looks like. Taking alpha from the centre alone would paint
    // an opaque dark band into the gap beside every column instead. Since
    // a >= tap.a, 0 <= rgb <= a holds. No clamp on rgb, so HDR captures keep
    // their headroom. On the UBO branch below is unfed zero, so the
    // compensation vanishes and the preview shows the plain per-tap split.
    vec3 below = stripBelowColor(uv).rgb;
    float a = max(base.a, max(rTap.a, bTap.a));
    vec3 rgb = vec3(rTap.r + below.r * (a - rTap.a), base.g + below.g * (a - base.a),
                    bTap.b + below.b * (a - bTap.a));
    return vec4(rgb, a);
}
