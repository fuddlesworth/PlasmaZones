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
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float kTau = 6.28318530718;

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

void main() {
    vec4 tex = surfaceTexel(vTexCoord);

    // Identity-decoration guard — mirrors border/effect.frag: a degenerate
    // frame rect would collapse the SDF to "edge everywhere".
    if (uSurfaceFrameSize.x < 1.0 || uSurfaceFrameSize.y < 1.0) {
        fragColor = tex;
        return;
    }

    vec2 p = surfacePixel(vTexCoord);
    const float aa = 0.7;

    float width = p_borderWidth * uSurfaceScale;
    float radius = (p_cornerRadius + p_borderWidth) * uSurfaceScale;

    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 cen = uSurfaceFrameTopLeft + halfSz;
    float r = clamp(radius, 0.0, min(halfSz.x, halfSz.y));

    vec2 q = abs(p - cen) - halfSz + r;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;

    float insideMask = 1.0 - smoothstep(-aa, aa, d);
    float edge = smoothstep(-width - aa, -width + aa, d);

    // Hue = perimeter angle scaled by the rainbow count, rotated by time.
    // hueTurns 1.0 wraps exactly one wheel around the frame so the seam at
    // the wrap point is invisible; fractional turn counts show a seam by
    // design (the parameter floor of 0.5 keeps it a soft one).
    vec2 rel = (p - cen) / max(halfSz, vec2(1.0));
    float u = atan(rel.y, rel.x) / kTau;
    float hue = fract(u * max(p_hueTurns, 0.5) - iTime * p_cycleSpeed);
    vec4 band = vec4(hsv2rgb(vec3(hue, clamp(p_saturation, 0.0, 1.0), clamp(p_brightness, 0.0, 1.0))), 1.0);

    // Focus cue: full-strength ring on the focused surface, dimmed otherwise.
    band.a *= mix(0.55, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));

    // Clip content to the inner rounded rect; lay the band over transparency,
    // premultiplied — identical composite to the border pack.
    float ba = edge * insideMask * band.a;
    vec4 contentPx = tex * (1.0 - edge);
    fragColor = vec4(band.rgb * ba, ba) + contentPx * (1.0 - ba);
}
