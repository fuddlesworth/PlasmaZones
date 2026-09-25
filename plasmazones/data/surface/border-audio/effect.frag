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

    float bottomRadius = p_roundBottomCorners >= 0.5 ? p_cornerRadius : 0.0;
    BorderBand bb =
        standardBorderBandSplit(surfacePixel(uv), p_borderWidth, p_cornerRadius, bottomRadius, p_edgeSoftness);

    // Base focus-mixed border colour, then react to the bass. getBassSoft() is
    // 0 when the audio visualizer is off, so the reactive terms vanish and this
    // reduces to the plain border.
    vec4 base = mix(p_inactiveColor, p_activeColor, clamp(uSurfaceFocused, 0.0, 1.0));
    float pulse = clamp(getBassSoft() * max(p_reactivity, 0.0), 0.0, 1.0);
    vec4 band = mix(base, p_pulseColor, pulse);
    band.a = clamp(band.a * (1.0 + pulse * 0.6), 0.0, 1.0);

    // NO second focus cue. The mix above is already the focus cue, the same
    // one the plain Border pack applies and never follows with an alpha dim.
    // Applying focusDim here as well rendered an unfocused Audio Border at 55%
    // of the alpha its own "Border colour when the window is unfocused"
    // parameter asks for, so it read as materially more transparent than every
    // other pack in the border family at identical colours.
    return borderComposite(tex, band, bb.edge, bb.insideMask);
}
