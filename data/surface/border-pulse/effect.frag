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
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float kTau = 6.28318530718;

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

    // Breathe: 0 at the A end, 1 at the B end, continuous across the wrap.
    float phase = 0.5 - 0.5 * cos(iTime * max(p_pulseSpeed, 0.0) * kTau);
    vec4 band = mix(p_colorA, p_colorB, phase);
    // Brightness dip rides the same phase so the low end of the crossfade
    // is also the dim point, one coherent heartbeat instead of two beats.
    band.a *= 1.0 - clamp(p_pulseDepth, 0.0, 1.0) * phase;

    // Focus cue: full-strength pulse on the focused surface, dimmed otherwise.
    band.a *= mix(0.55, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));

    // Clip content to the inner rounded rect; lay the band over transparency,
    // premultiplied — identical composite to the border pack.
    float ba = edge * insideMask * band.a;
    vec4 contentPx = tex * (1.0 - edge);
    fragColor = vec4(band.rgb * ba, ba) + contentPx * (1.0 - ba);
}
