// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glow surface shader — an OUTER glow in the style of the Oxygen window
// decoration: the surface content passes through untouched, and a soft
// coloured halo radiates outward from the surface's silhouette into the
// transparent margin around it (the drop-shadow region on the compositor).
//
// The halo source is the separable Gaussian blur of the input surface
// (iChannel1, from buffer0.frag -> buffer1.frag): the blurred ALPHA bleeds
// past the silhouette, so masking it to where the surface itself is
// transparent yields a halo that follows the exact shape earlier packs
// produced (rounded corners from a Border pack ahead of it in the chain,
// shaped windows, anything). This pack knows nothing about borders or
// corner radius — it is a pure compositing layer over the prior output.
//
// Like Oxygen's active-window glow, the halo tracks focus: full strength on
// the focused surface, dimmed on unfocused ones. Strength and tint are the
// pack's parameters. Static (no iTime).

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 base = surfaceTexel(vTexCoord);           // the input surface (prior pack's output)
    vec4 blur = texture(iChannel1, vTexCoord);     // separable-Gaussian blurred surface

    // Outer-only mask: the blurred silhouette alpha, confined to where the
    // surface itself is transparent. Inside opaque content this is 0, so the
    // content is passed through byte-for-byte; at the AA rim it fades in.
    // The widen + gamma shape the Gaussian tail into a fuller, Oxygen-like
    // ramp: bright against the edge, falling off smoothly outward.
    float sil = clamp(blur.a * 1.6, 0.0, 1.0);
    float halo = pow(sil, 1.7) * (1.0 - base.a);

    // Focus tracking: full halo on the focused surface, dimmed otherwise —
    // the glow reads as the "active window" cue, like Oxygen's decoration.
    halo *= p_glowStrength * mix(0.30, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));

    // Premultiplied additive-over: the halo lights the transparent margin
    // under its own alpha; the content term is untouched.
    float haloA = clamp(halo * p_glowColor.a, 0.0, 1.0);
    fragColor = vec4(base.rgb + p_glowColor.rgb * haloA, clamp(base.a + haloA, 0.0, 1.0));
}
