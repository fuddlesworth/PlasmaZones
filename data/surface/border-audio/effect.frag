// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Audio border surface shader — a rounded border whose colour and brightness
// pulse with the CAVA audio spectrum. The base look is the plain border (focus-
// mixed active/inactive colour); the bass energy pushes the band toward a pulse
// colour and lifts its alpha on each beat, gated so a weak signal doesn't jitter.
//
// Audio reaches this pack on both runtimes when the audio visualizer is
// enabled: the daemon feeds the spectrum to its OSD / popup surfaces, and the
// KWin effect runs its own CAVA provider to feed window borders. When the
// visualizer is off, getBassSoft() reads 0 and the pack renders as a plain
// static border (a graceful, still-useful fallback).
//
// Demonstrates two of the surface API additions at once: the pSurface entry
// scaffold (no hand-written main()) and the opt-in surface_audio.glsl module.

#include <surface_audio.glsl>

vec4 pSurface(vec2 uv) {
    vec4 tex = surfaceTexel(uv);

    if (surfaceFrameDegenerate()) {
        return tex;
    }

    vec2 p = surfacePixel(uv);
    const float aa = 0.7;

    float width = p_borderWidth * uSurfaceScale;
    float radius = (p_cornerRadius + p_borderWidth) * uSurfaceScale;

    FrameSDF fs = frameSdf(p, radius);
    float insideMask = 1.0 - smoothstep(-aa, aa, fs.d);
    float edge = smoothstep(-width - aa, -width + aa, fs.d);

    // Base focus-mixed border colour, then react to the bass. getBassSoft() is
    // 0 when the audio visualizer is off, so the reactive terms vanish and this
    // reduces to the plain border.
    vec4 base = mix(p_inactiveColor, p_activeColor, clamp(uSurfaceFocused, 0.0, 1.0));
    float pulse = clamp(getBassSoft() * max(p_reactivity, 0.0), 0.0, 1.0);
    vec4 band = mix(base, p_pulseColor, pulse);
    band.a = clamp(band.a * (1.0 + pulse * 0.6), 0.0, 1.0);

    band.a *= focusDim(0.55);

    return borderComposite(tex, band, edge, insideMask);
}
