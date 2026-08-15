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
    float m = stripMask(uv, p_edgeFeather);
    // Signed velocity in output-widths per second. The tilt saturates well
    // before anything folds over.
    float tilt = clamp(iStripMotion.w * 0.35 * p_tilt, -0.22, 0.22);
    if (abs(tilt) < 1.0e-4 || m < 1.0e-3) {
        return getStripColor(uv);
    }
    // Perspective-ish remap around the WORK AREA's centre, not the output's:
    // under a side panel the two differ, and pivoting on the output centre
    // would tilt the drum about a point the columns are not centred on. sx is
    // the signed distance from that centre in output uv, so the remap stays
    // in the space getStripColor samples.
    float centreX = 0.5;
    float centreY = 0.5;
    if (iStripRect.z > 0.0 && iStripRect.w > 0.0) {
        centreX = (iStripRect.x + iStripRect.z * 0.5) / max(iResolution.x, 1.0);
        centreY = (iStripRect.y + iStripRect.w * 0.5) / max(iResolution.y, 1.0);
    }
    // The drum turns about the axis the strip TRAVELS on, so the whole warp is
    // written in along/across terms rather than x/y. iStripAxis is the unit
    // vector along travel; its perpendicular is the across direction. For a
    // horizontal strip these reduce to exactly the old uv.x / uv.y.
    vec2 axisPerp = vec2(iStripAxis.y, iStripAxis.x);
    vec2 centre = vec2(centreX, centreY);
    float along = dot(uv, iStripAxis);
    float across = dot(uv, axisPerp);
    float centreAlong = dot(centre, iStripAxis);
    float centreAcross = dot(centre, axisPerp);

    float sx = along - centreAlong;
    float depth = 1.0 + tilt * sx * 2.0;
    // Sampling FURTHER from the centre means the content reads as compressed,
    // so the receding side (tilt * sx > 0) squeezes ALONG the strip. The
    // across taper only ever swells the NEAR side: letting the receding side
    // sample outward too would run the sample off the far edges of the capture
    // and band those rows black, and the along squeeze plus the shading below
    // already carry the drum.
    float warpedAlong = centreAlong + sx * depth;
    float warpedAcross = centreAcross + (across - centreAcross) * (1.0 + min(tilt * sx, 0.0) * 0.9);
    vec2 warped = iStripAxis * warpedAlong + axisPerp * warpedAcross;
    vec2 su = mix(uv, warped, m);
    vec4 c = getStripColor(su);
    // Depth cue: the compressed side falls into shade, scaled with the same
    // mask and tilt so it vanishes at settle. p_shading is clamped because a
    // hand-edited profile could otherwise drive it negative and blow the
    // colour past white, or past 1 and drive it negative.
    float shade = 1.0 - clamp(p_shading, 0.0, 1.0) * 0.35 * clamp(tilt * sx * 6.0, 0.0, 1.0) * m;
    // Off the drum: on the receding side the warp legitimately asks for
    // content past the screen edge, which the clamped capture would answer
    // by smearing its last pixel row/column. Fade those samples into shadow
    // instead: the tilted plane's far edge pulls away from the screen edge
    // and darkness shows behind it, which is the depth cue a real drum
    // would give. Vanishes with the tilt, so settle stays the identity.
    // Ramp INTO the void before the edge rather than after it: a sample is
    // already clamped the instant it leaves [0,1], so a ramp starting at 0
    // would show a band of half-lit smear. Starting the ramp 0.006 uv inside
    // means the void is fully closed by the time clamping begins.
    vec2 outside = max(-su, su - 1.0);
    float voidAmt = smoothstep(-0.006, 0.0, max(outside.x, outside.y));
    return vec4(c.rgb * shade * (1.0 - voidAmt), c.a);
}
