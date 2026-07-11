// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor glass surface shader — the Phosphor set's blur pane, and the one
// pack that takes the project name literally: the glass behaves like a
// phosphor screen. The scene behind the surface is Gaussian-blurred and sunk
// toward the deep navy brand surface, and wherever the backdrop is BRIGHT the
// glass is excited into an afterglow in the brand accent gradient (cyan
// #22D3EE → blue #3B82F6 → purple #A855F7 → rose #F43F5E) — light behind the
// window literally charges the pane. The excited response breathes slowly
// (phosphor persistence), and a soft diagonal recharge sweep passes over the
// pane, briefly lifting even dim regions. Luminance-reactive, unlike every
// other blur pack: move a bright window behind this glass and the glow
// follows it.
//
// DAEMON FALLBACK: daemon-hosted surfaces (OSDs / popups) have no scene
// behind them (uHasBackdrop = 0), so the pack renders a translucent navy
// slab with the same gradient shimmer and recharge sweep.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the phosphor backdrop.

#include <surface_multipass.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose.
vec3 fluxGradient(float t) {
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(p_colorCyan.rgb, p_colorBlue.rgb, clamp(t, 0.0, 1.0));
    c = mix(c, p_colorPurple.rgb, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, p_colorRose.rgb, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

// Seamless ping-pong of an unbounded coordinate into [0, 1].
float pingPong(float x) {
    x = fract(x);
    return 1.0 - abs(2.0 * x - 1.0);
}

vec4 pSurface(vec2 uv) {
    SurfaceSlab slab = surfaceSlabOpen(uv, p_cornerRadius * uSurfaceScale);

    // Frame-normalized coordinate: gradient hue, sweep phase and vignette all
    // scale with the pane.
    vec2 fuv = frameUv(slab.px);
    float diag = (fuv.x + fuv.y) * 0.5;

    // ── Recharge sweep: a soft diagonal band drifting across the pane on a
    // slow clock. It boosts the phosphor response as it passes, so the glass
    // visibly re-energises rather than sitting at a steady glow. ──
    float sweepPhase = fract(diag * 0.7 - iTime * max(p_sweepSpeed, 0.0)) - 0.5;
    float sweep = exp(-sweepPhase * sweepPhase / 0.008);

    // Gradient hue flows gently through the pane.
    vec3 glowCol = fluxGradient(pingPong(fuv.x * 0.55 + fuv.y * 0.30 + iTime * max(p_flowSpeed, 0.0)));

    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    float glowStrength = clamp(p_glowStrength, 0.0, 2.0);
    vec3 navy = p_colorTint.rgb;

    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        vec4 blurred = texture(iChannel1, uv);

        // Un-premultiplied backdrop luminance drives the excitation.
        float lumN = blurred.a > 0.001
            ? dot(blurred.rgb / blurred.a, vec3(0.299, 0.587, 0.114))
            : 0.0;

        // ── Phosphor excitation: bright backdrop charges the glass. The
        // response breathes slowly (persistence), and the sweep both boosts
        // charged regions and faintly lights dim ones as it passes. ──
        float excite = pow(smoothstep(clamp(p_exciteThreshold, 0.0, 1.0), 1.0, lumN), 1.5);
        float persist = 0.75 + 0.25 * sin(iTime * 1.3 + lumN * 6.0 + diag * 4.0);
        float response = excite * persist * (1.0 + sweep * 0.8) + sweep * 0.06;

        // Base pane: the blurred scene sunk toward the navy brand surface.
        vec3 base = mix(blurred.rgb, navy * blurred.a, tintStrength);

        // Soft multiplicative vignette so the pane reads as a pane.
        float vignette = clamp(1.0 - length((fuv - 0.5) * vec2(0.3, 1.0)) * 0.15, 0.0, 1.0);

        vec3 color = (base + glowCol * response * glowStrength * blurred.a) * vignette;
        color = clamp(color, 0.0, max(blurred.a, 0.0001));
        pane = vec4(color, blurred.a) * slab.mask;
    } else {
        // No scene behind the surface: a translucent navy slab with the same
        // gradient shimmer and recharge sweep, so OSDs still read on-brand.
        float shimmer = 0.10 + sweep * 0.12;
        vec3 color = clamp(navy + glowCol * shimmer * glowStrength, 0.0, 1.0);
        float slabAlpha = clamp(0.35 + 0.45 * tintStrength, 0.0, 1.0);
        pane = vec4(color, 1.0) * slabAlpha * slab.mask;
    }

    return slabComposite(slab.window, pane);
}
