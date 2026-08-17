// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Strip Jelly — a soft-body bow on the scrolling strip. The middle of the
// strip ACROSS its travel axis leads and the rows to either side of it lag,
// so the content bows like jelly in the direction of travel. On a horizontal
// strip that reads as the vertical middle leading with the top and bottom
// rows lagging. On a vertical strip it is the horizontal middle leading with
// the left and right columns lagging. The lag is a pure displacement ALONG
// the travel axis driven by velocity, so it relaxes to the identity image as
// the spring settles.
//
// Strip contract (strip_transition.glsl): intensity keys off iStripMotion,
// which converges to zero at settle.
#include <strip_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
    float m = stripMask(uv, p_edgeFeather);
    // Signed velocity in output-extents along the travel axis per second,
    // saturating at ~4% of the output extent along that axis of bow at full
    // wobble.
    float lag = clamp(iStripMotion.w * 0.06 * p_wobble, -0.04, 0.04) * m;
    // Die out before the lagged rows can sample past the screen edge, where
    // the clamped capture would stretch the last column (see stripEdgeFade).
    lag *= stripEdgeFade(uv, 0.08);
    if (abs(lag) < 1.0e-5) {
        return getStripColor(uv);
    }
    // Quadratic profile ACROSS the strip's travel axis, inside the work area:
    // zero at the across-centre, full at the two edges either side of it.
    // Content on a lagging row has not caught up with the middle yet, so it is
    // sampled from where it still is: behind, along the travel direction.
    //
    // The profile MUST be measured across the axis, never on a fixed uv
    // component. stripUv(uv).y is the across coordinate only on a horizontal
    // strip. On a vertical one it is the ALONG coordinate, which makes the
    // profile collinear with the displacement and turns the bow into a shear
    // that is zero at the strip's middle and maximal at its two ends. That is
    // a different effect, not a rotated one.
    //
    // stripUv runs past 0..1 in the feather band outside the work area, where
    // the mask has not yet reached zero. Clamp the profile so the bow never
    // exceeds the amplitude the stripEdgeFade budget above was sized for.
    float across = dot(stripUv(uv), stripAxisPerp());
    float edge = min(abs(across - 0.5) * 2.0, 1.0);
    float prof = edge * edge;
    return getStripColor(uv + stripAxisOffset(lag * prof));
}
