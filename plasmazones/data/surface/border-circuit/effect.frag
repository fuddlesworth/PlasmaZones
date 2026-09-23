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

#include <surface_noise.glsl>

const int kMaxPulses = 6;

vec4 pSurface(vec2 uv) {
    vec4 tex = surfaceTexel(uv);

    if (surfaceFrameDegenerate()) {
        return tex;
    }

    // Band geometry: the family's OUTER-radius rounded-rect SDF, content clip
    // and band edge from this pack's logical-px width and corner radius.
    vec2 p = surfacePixel(uv);
    BorderBand bb = standardBorderBand(p, p_borderWidth, p_cornerRadius);

    // ── Hex circuit texture in device px, so cells stay square-ish at any
    // window size and DPI. Walls carry the grid colour; interiors stay
    // darker, and each cell flickers faintly on a slow hashed clock.
    vec2 scaled = p / (max(p_hexSize, 2.0) * max(uSurfaceScale, 0.001));
    vec2 hex = hexLocal(scaled);
    float hd = hexDist(hex);
    vec2 cellId = floor((scaled - hex) / vec2(1.0, 1.732));
    float wall = smoothstep(0.34, 0.48, hd);
    float flick = 0.8 + 0.2 * hash13(cellId + floor(iTime * 2.0));

    // ── Perimeter pulses: evenly-phased bright points orbiting the band,
    // each a tight falloff along the perimeter coordinate.
    float u = framePerimeter(p, bb.fs.center, bb.fs.halfSize); // -0.5 .. 0.5
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
    band.a *= focusDim(0.55);

    return borderComposite(tex, band, bb.edge, bb.insideMask);
}
