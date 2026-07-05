// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Circuit border surface shader — the Border pack's rounded-rect clip +
// border band, with the band textured as a hexagonal circuit grid: dim
// cell walls in the grid colour, faint per-cell flicker, and a handful
// of bright data pulses travelling around the perimeter lighting the
// cells they pass. The Ghost-in-the-Shell hex aesthetic of the
// aretha-shell shaders, folded into the border family's geometry. The
// perimeter coordinate is the frame-normalised angle (uniform pulse
// speed per side on non-square windows, same approximation as the
// marching-ants pack). Dims when the surface is unfocused.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float kTau = 6.28318530718;
const int kMaxPulses = 6;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// High-quality hash, avoids directional artifacts (same as frosted-glass).
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Hex helpers: distance to the cell centre in hex metric (0 centre ..
// ~0.5 wall) and the cell-local offset for a point in hex-grid space.
float hexDist(vec2 p) {
    p = abs(p);
    return max(p.x * 0.866025 + p.y * 0.5, p.y);
}

vec2 hexLocal(vec2 uv) {
    vec2 r = vec2(1.0, 1.732);
    vec2 h = r * 0.5;
    vec2 a = mod(uv, r) - h;
    vec2 b = mod(uv - h, r) - h;
    return dot(a, a) < dot(b, b) ? a : b;
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

    // ── Hex circuit texture in device px, so cells stay square-ish at any
    // window size and DPI. Walls carry the grid colour; interiors stay
    // darker, and each cell flickers faintly on a slow hashed clock.
    vec2 scaled = p / (max(p_hexSize, 2.0) * uSurfaceScale);
    vec2 hex = hexLocal(scaled);
    float hd = hexDist(hex);
    vec2 cellId = floor((scaled - hex) / vec2(1.0, 1.732));
    float wall = smoothstep(0.34, 0.48, hd);
    float flick = 0.8 + 0.2 * hash(cellId + floor(iTime * 2.0));

    // ── Perimeter pulses: evenly-phased bright points orbiting the band,
    // each a tight falloff along the perimeter coordinate.
    vec2 rel = (p - cen) / max(halfSz, vec2(1.0));
    float u = atan(rel.y, rel.x) / kTau; // -0.5 .. 0.5
    float count = clamp(p_pulseCount, 1.0, float(kMaxPulses));
    float pulse = 0.0;
    for (int i = 0; i < kMaxPulses; ++i) {
        if (float(i) >= count) {
            break;
        }
        float phase = fract(u - iTime * p_pulseSpeed - float(i) / count);
        float ring = min(phase, 1.0 - phase); // wrap-around distance
        pulse += exp(-ring * ring * 1400.0);
    }
    pulse = clamp(pulse, 0.0, 1.0);

    // Base grid: walls at full grid alpha, interiors dimmer, flicker on
    // both; pulses lift the cells they pass toward the pulse colour and
    // brighten the walls hardest (a lit trace, not a flat flash).
    vec4 band;
    band.rgb = mix(p_colorA.rgb, p_colorB.rgb, pulse);
    float gridA = p_colorA.a * mix(0.35, 1.0, wall) * flick;
    band.a = clamp(gridA + pulse * p_colorB.a * mix(0.6, 1.0, wall), 0.0, 1.0);

    // Focus cue: full-strength circuit on the focused surface, dimmed otherwise.
    band.a *= mix(0.55, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));

    // Clip content to the inner rounded rect; lay the band over transparency,
    // premultiplied — identical composite to the border pack.
    float ba = edge * insideMask * band.a;
    vec4 contentPx = tex * (1.0 - edge);
    fragColor = vec4(band.rgb * ba, ba) + contentPx * (1.0 - ba);
}
