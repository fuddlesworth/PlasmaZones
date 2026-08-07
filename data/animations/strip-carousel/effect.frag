// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Strip Carousel — a light perspective tilt on the scrolling strip, as if
// the columns sit on a drum being spun. The side the content travels toward
// recedes (compressed and shaded), the other side comes forward, and the
// drum flattens back to the identity image as the spring settles.
//
// The perspective is a cheap screen-space fake (nonlinear horizontal
// remap plus a vertical taper toward the receding side), not a projection.
// The displacement is masked by the strip's work area so panels and the
// wallpaper margins stay put.
//
// Strip contract (strip_transition.glsl): intensity keys off iStripMotion,
// which converges to zero at settle.
#include <strip_transition.glsl>

vec4 pTransition(vec2 uv, float t) {
    float m = stripMask(uv, 0.04);
    // Signed velocity in output-widths per second. The tilt saturates well
    // before anything folds over.
    float tilt = clamp(iStripMotion.w * 0.35 * p_tilt, -0.22, 0.22);
    if (abs(tilt) < 1.0e-4 || m < 1.0e-3) {
        return getStripColor(uv);
    }
    // Perspective-ish remap around the output center. sx in [-0.5, 0.5];
    // the receding side (sign of travel) compresses, the near side expands,
    // and rows taper vertically toward the receding side.
    float sx = uv.x - 0.5;
    float depth = 1.0 + tilt * sx * 2.0;
    vec2 warped;
    warped.x = 0.5 + sx * depth;
    warped.y = 0.5 + (uv.y - 0.5) * (1.0 + tilt * sx * 0.9);
    vec2 su = mix(uv, warped, m);
    vec4 c = getStripColor(su);
    // Depth cue: the compressed side falls into shade, scaled with the same
    // mask and tilt so it vanishes at settle.
    float shade = 1.0 - p_shading * 0.35 * clamp(tilt * sx * 6.0, 0.0, 1.0) * m;
    return vec4(c.rgb * shade, c.a);
}
