// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pulse border surface shader — the Border pack's rounded-rect clip +
// border band, with the band colour breathing between two colours on a
// cosine cycle and an optional brightness dip at the low end. Unlike the
// sweep and marching packs the whole band changes uniformly, so the
// effect reads as a heartbeat rather than motion around the frame. Dims
// when the surface is unfocused, matching the family's focus cue.
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

    vec2 p = surfacePixel(vTexCoord);
    const float aa = 0.7;

    float width = p_borderWidth * uSurfaceScale;
    float radius = (p_cornerRadius + p_borderWidth) * uSurfaceScale;

    FrameSDF fs = frameSdf(p, radius);
    float insideMask = 1.0 - smoothstep(-aa, aa, fs.d);
    float edge = smoothstep(-width - aa, -width + aa, fs.d);

    // Breathe: 0 at the A end, 1 at the B end, continuous across the wrap.
    float phase = 0.5 - 0.5 * cos(iTime * max(p_pulseSpeed, 0.0) * TAU);
    vec4 band = mix(p_colorA, p_colorB, phase);
    // Brightness dip rides the same phase so the low end of the crossfade
    // is also the dim point, one coherent heartbeat instead of two beats.
    band.a *= 1.0 - clamp(p_pulseDepth, 0.0, 1.0) * phase;

    // Focus cue: full-strength pulse on the focused surface, dimmed otherwise.
    band.a *= focusDim(0.55);

    fragColor = borderComposite(tex, band, edge, insideMask);
}
