// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Strip Jelly — a soft-body bow on the scrolling strip. The vertical middle
// of the strip leads and the rows toward the top and bottom edges lag, so
// the content bows like jelly in the direction of travel. The lag is a pure
// horizontal warp driven by velocity, so it relaxes to the identity image
// as the spring settles.
//
// Strip contract (strip_transition.glsl): intensity keys off iStripMotion,
// which converges to zero at settle.
#include <strip_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
    float m = stripMask(uv, 0.03);
    // Signed velocity in output-widths per second, saturating at ~4% of the
    // output width of bow at full wobble.
    float lag = clamp(iStripMotion.w * 0.06 * p_wobble, -0.04, 0.04) * m;
    // Die out before the lagged rows can sample past the screen edge, where
    // the clamped capture would stretch the last column (see stripEdgeFade).
    lag *= stripEdgeFade(uv, 0.08);
    if (abs(lag) < 1.0e-5) {
        return getStripColor(uv);
    }
    // Quadratic vertical profile inside the strip's work area: zero at the
    // vertical center, full at the top and bottom edges. Content at a lagging
    // row has not caught up with the middle yet, so it is sampled from where
    // it still is: behind, along the travel direction.
    float sy = stripUv(uv).y;
    float prof = pow(abs(sy - 0.5) * 2.0, 2.0);
    return getStripColor(uv + vec2(lag * prof, 0.0));
}
