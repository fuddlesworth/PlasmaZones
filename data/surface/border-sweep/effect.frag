// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Sweep border surface shader — the Border pack's rounded-rect clip +
// border band, with the band colour replaced by a travelling two-colour
// sweep. An angular gradient (centred on the frame) circulates around the
// perimeter at p_sweepSpeed rotations per second, blending p_colorA and
// p_colorB in a seamless A -> B -> A loop; the sweep dims when the surface
// is unfocused so the motion reads as the active-window cue.
//
// The perimeter coordinate is the frame-normalised angle (divided by the
// half extents), so the sweep speed stays uniform per side on non-square
// windows — matching the marching / circuit / rgb packs. p_borderWidth /
// p_cornerRadius are logical px, scaled to device px by uSurfaceScale.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

#version 450
#include <surface_lib.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 tex = surfaceTexel(vTexCoord);

    if (surfaceFrameDegenerate()) {
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

    FrameSDF fs = frameSdf(p, radius);
    float insideMask = 1.0 - smoothstep(-aa, aa, fs.d);
    float edge = smoothstep(-width - aa, -width + aa, fs.d);

    // Travelling sweep: the fragment's aspect-normalised angle around the
    // frame centre, offset by time, picks a phase in a seamless two-colour
    // loop. cos makes the A -> B -> A blend continuous across the wrap, so
    // no seam ever shows.
    float angle = framePerimeter(p, fs.center, fs.halfSize); // -0.5 .. 0.5
    float phase = angle - iTime * p_sweepSpeed;
    vec4 sweep = mix(p_colorA, p_colorB, 0.5 - 0.5 * cos(phase * TAU));

    // Focus cue: full-strength sweep on the focused surface, dimmed otherwise.
    sweep.a *= focusDim(0.55);

    fragColor = borderComposite(tex, sweep, edge, insideMask);
}
