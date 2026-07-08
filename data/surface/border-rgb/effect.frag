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

#version 450
#include <surface_lib.glsl>
#include <surface_color.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 tex = surfaceTexel(vTexCoord);

    if (surfaceFrameDegenerate()) {
        fragColor = tex;
        return;
    }

    vec2 p = surfacePixel(vTexCoord);
    const float aa = 0.7;

    float width = p_borderWidth * uSurfaceScale;
    float radius = (p_cornerRadius + p_borderWidth) * uSurfaceScale;

    FrameSDF fs = frameSdf(p, radius);
    float insideMask = 1.0 - smoothstep(-aa, aa, fs.d);
    float edge = smoothstep(-width - aa, -width + aa, fs.d);

    // Hue = perimeter angle scaled by the rainbow count, rotated by time.
    // hueTurns 1.0 wraps exactly one wheel around the frame so the seam at
    // the wrap point is invisible; fractional turn counts show a seam by
    // design (the parameter floor of 0.5 keeps it a soft one).
    float u = framePerimeter(p, fs.center, fs.halfSize);
    float hue = fract(u * max(p_hueTurns, 0.5) - iTime * p_cycleSpeed);
    vec4 band = vec4(hsv2rgb(vec3(hue, clamp(p_saturation, 0.0, 1.0), clamp(p_brightness, 0.0, 1.0))), 1.0);

    // Focus cue: full-strength ring on the focused surface, dimmed otherwise.
    band.a *= focusDim(0.55);

    fragColor = borderComposite(tex, band, edge, insideMask);
}
