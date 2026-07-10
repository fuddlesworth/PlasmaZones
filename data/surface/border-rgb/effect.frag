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

    // Band geometry: the family's OUTER-radius rounded-rect SDF, content clip
    // and band edge from this pack's logical-px width and corner radius.
    vec2 p = surfacePixel(uv);
    BorderBand bb = standardBorderBand(p, p_borderWidth, p_cornerRadius);

    // Hue = perimeter angle scaled by the rainbow count, rotated by time.
    // hueTurns 1.0 wraps exactly one wheel around the frame so the seam at
    // the wrap point is invisible; fractional turn counts show a seam by
    // design (the parameter floor of 0.5 keeps it a soft one).
    float u = framePerimeter(p, bb.fs.center, bb.fs.halfSize);
    float hue = fract(u * max(p_hueTurns, 0.5) - iTime * p_cycleSpeed);
    vec4 band = vec4(hsv2rgb(vec3(hue, clamp(p_saturation, 0.0, 1.0), clamp(p_brightness, 0.0, 1.0))), 1.0);

    // Focus cue: full-strength ring on the focused surface, dimmed otherwise.
    band.a *= focusDim(0.55);

    return borderComposite(tex, band, bb.edge, bb.insideMask);
}
