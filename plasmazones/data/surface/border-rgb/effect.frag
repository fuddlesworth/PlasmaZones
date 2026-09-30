// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// RGB cycle border surface shader — the Border pack's rounded-rect clip +
// border band, with the band painted as a hue wheel wrapped around the
// frame and rotated over time. The perimeter coordinate is the
// frame-normalised angle (same approximation as the marching-ants pack).
// cycleSpeed 0 freezes the wheel into a still rainbow ring. Dims when the
// surface is unfocused, matching the family's focus cue.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

#include <surface_color.glsl>

vec4 pSurface(vec2 uv) {
    vec4 tex = surfaceTexel(uv);

    if (surfaceFrameDegenerate()) {
        return tex;
    }

    // Band geometry: the family's rounded-rect SDF (outer radius = content radius +
    // width, except at a zero end, which stays square), content clip
    // and band edge from this pack's logical-px width and corner radius.
    vec2 p = surfacePixel(uv);
    float bottomRadius = surfaceBottomRadius(p_cornerRadius, p_roundBottomCorners);
    BorderBand bb = standardBorderBandSplit(p, p_borderWidth, p_cornerRadius, bottomRadius, p_edgeSoftness);

    // Hue = perimeter angle scaled by the rainbow count, rotated by time.
    //
    // framePerimeter returns [-0.5, 0.5], so at the wrap point the hue jumps
    // by fract(hueTurns). An INTEGER turn count therefore makes the jump zero
    // and the seam invisible; every other value shows one, and the size of the
    // jump is the fractional part rather than anything the 0.5 floor bounds.
    // At 0.5 exactly the jump is half the wheel, which is the LARGEST possible
    // seam, not a soft one.
    float u = framePerimeter(p, bb.fs.center, bb.fs.halfSize);
    float hue = fract(u * max(p_hueTurns, 0.5) - iTime * p_cycleSpeed);
    vec4 band = vec4(hsv2rgb(vec3(hue, clamp(p_saturation, 0.0, 1.0), clamp(p_brightness, 0.0, 1.0))), 1.0);

    // Focus cue: full-strength ring on the focused surface, dimmed otherwise.
    band.a *= focusDim(0.55);

    return borderComposite(tex, band, bb.edge, bb.insideMask);
}
