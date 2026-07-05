// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Marching-ants border surface shader — the Border pack's rounded-rect
// clip + border band, with the band split into dashes that march around
// the perimeter (the classic selection marquee). The perimeter coordinate
// is the fragment's angle around the frame centre normalised by the
// frame's half extents, which keeps dash spacing roughly uniform per side
// on non-square windows (exact arc-length parameterisation of a rounded
// rect is not worth the shader cost for a decorative band). Dashes dim
// when the surface is unfocused, matching the sweep pack's focus cue.
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

    // Perimeter coordinate: angle normalised by the half extents so a wide
    // frame does not stretch its top and bottom dashes. Marching offset in
    // whole perimeter revolutions per second, like the sweep pack.
    vec2 rel = (p - cen) / max(halfSz, vec2(1.0));
    float u = atan(rel.y, rel.x) / kTau; // -0.5 .. 0.5
    float cellPos = fract((u - iTime * p_marchSpeed) * max(p_dashCount, 1.0));
    // Antialias the dash edges over a fixed fraction of the cell so the
    // marquee reads crisp at any dash count without shimmering.
    float fill = clamp(p_dashFill, 0.0, 1.0);
    float dash = smoothstep(0.0, 0.06, cellPos) * (1.0 - smoothstep(fill - 0.06, fill, cellPos));

    vec4 band = mix(p_colorB, p_colorA, dash);

    // Focus cue: full-strength dashes on the focused surface, dimmed otherwise.
    band.a *= mix(0.55, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));

    // Clip content to the inner rounded rect; lay the band over transparency,
    // premultiplied — identical composite to the border pack.
    float ba = edge * insideMask * band.a;
    vec4 contentPx = tex * (1.0 - edge);
    fragColor = vec4(band.rgb * ba, ba) + contentPx * (1.0 - ba);
}
