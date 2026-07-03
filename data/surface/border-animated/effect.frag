// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Animated border surface shader — the Border pack's rounded-rect clip +
// border band, with the band colour replaced by a travelling two-colour
// sweep. An angular gradient (centred on the frame) circulates around the
// perimeter at p_sweepSpeed rotations per second, blending p_colorA and
// p_colorB in a seamless A -> B -> A loop; the sweep dims when the surface
// is unfocused so the motion reads as the active-window cue.
//
// Geometry is identical to border/effect.frag: one analytic rounded-rect SDF
// over the frame rect clips the content to the inner rounded rect and lays
// the band over the background, so a translucent band blends with what is
// behind the surface. p_borderWidth / p_cornerRadius are logical px, scaled
// to device px by uSurfaceScale.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float kTau = 6.28318530718;

void main() {
    vec4 tex = surfaceTexel(vTexCoord);

    // Identity-decoration state: before a host wires real geometry the frame
    // rect is degenerate (uSurfaceFrameSize == 0). The SDF below would
    // collapse to "edge everywhere" and paint a band over the whole surface,
    // so pass the captured content through untouched until a real frame
    // arrives. Mirrors border/effect.frag's guard.
    if (uSurfaceFrameSize.x < 1.0 || uSurfaceFrameSize.y < 1.0) {
        fragColor = tex;
        return;
    }

    // Fragment's top-down device pixel; the content rect sits at
    // uSurfaceFrameTopLeft..+uSurfaceFrameSize (device px).
    vec2 p = surfacePixel(vTexCoord);
    const float aa = 0.7;

    // Pack params are logical px — scale to the device-px geometry space.
    float width = p_borderWidth * uSurfaceScale;
    // OUTER radius = content radius + width, so the band sits inside it and
    // the content corner ends one band-width in, at p_cornerRadius.
    float radius = (p_cornerRadius + p_borderWidth) * uSurfaceScale;

    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 cen = uSurfaceFrameTopLeft + halfSz;
    float r = clamp(radius, 0.0, min(halfSz.x, halfSz.y));

    vec2 q = abs(p - cen) - halfSz + r;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;

    float insideMask = 1.0 - smoothstep(-aa, aa, d);
    float edge = smoothstep(-width - aa, -width + aa, d);

    // Travelling sweep: the fragment's angle around the frame centre, offset
    // by time, picks a phase in a seamless two-colour loop. cos makes the
    // A -> B -> A blend continuous across the wrap, so no seam ever shows.
    float angle = atan(p.y - cen.y, p.x - cen.x) / kTau; // -0.5 .. 0.5
    float phase = angle - iTime * p_sweepSpeed;
    vec4 sweep = mix(p_colorA, p_colorB, 0.5 - 0.5 * cos(phase * kTau));

    // Focus cue: full-strength sweep on the focused surface, dimmed otherwise.
    sweep.a *= mix(0.55, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));

    // Clip content to the inner rounded rect; lay the band over transparency,
    // premultiplied. width <= 0 (no border in the chain's params) leaves the
    // content rounded with no band.
    float ba = edge * insideMask * sweep.a;
    vec4 contentPx = tex * (1.0 - edge);
    fragColor = vec4(sweep.rgb * ba, ba) + contentPx * (1.0 - ba);
}
