// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor glass surface shader — the Phosphor set's blur pane, and the one
// pack that takes the project name literally: the glass behaves like a
// phosphor screen. The scene behind the surface is blurred by the dual Kawase chain and sunk
// toward the deep navy brand surface, and wherever the backdrop is BRIGHT the
// glass is excited into an afterglow in the brand accent gradient (cyan
// #22D3EE → blue #3B82F6 → purple #A855F7 → rose #F43F5E) — light behind the
// window literally charges the pane. The excited response breathes slowly
// (phosphor persistence), and a soft diagonal recharge sweep passes over the
// pane, briefly lifting even dim regions. Luminance-reactive, like Duotone
// and unlike the rest of the family: move a bright window behind this glass
// and the glow follows it. Duotone reads luminance too, but maps it to a
// fixed two-colour ramp rather than driving an emissive response.
//
// SHARED BACKDROP STAGES, in order: the blurred sample runs through
// surfaceBackdropGrade (brightness, contrast, OKLab saturation, vibrancy)
// before the phosphor response is computed from it, and a driver-stable
// grain goes on last so it dithers the finished pane.
//
// NO-BACKDROP FALLBACK: when the host bound nothing behind the surface
// (uHasBackdrop = 0), the pack renders a translucent navy slab with the
// same gradient shimmer and recharge sweep.
//
// Retired handlesOpacity contract: uSurfaceOpacity is a constant 1.0 now
// (SetOpacity is layer-backed and custom chains own their alpha). The pack's
// own contentOpacity parameter fades the window content so the phosphor
// backdrop shows on opaque windows; a theme's own translucent pixels reveal
// it the same way with no parameter involved.

#include <surface_multipass.glsl>
#include <surface_noise.glsl>
#include <surface_color.glsl>

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
    float cornerPx = p_cornerRadius * uSurfaceScale;
    SurfaceSlab slab = surfaceSlabOpen(uv, cornerPx, p_roundBottomCorners >= 0.5 ? cornerPx : 0.0, p_edgeSoftness);
    // Fade the window content over the pane; the translucency it frees is
    // filled by the phosphor glass in slabComposite below.
    slab.window *= clamp(p_contentOpacity, 0.0, 1.0);

    // Frame-normalized coordinate: gradient hue, sweep phase and vignette all
    // scale with the pane.
    vec2 fuv = frameUv(slab.px);
    float diag = (fuv.x + fuv.y) * 0.5;

    // ── Recharge sweep: a soft diagonal band drifting across the pane on a
    // slow clock. It boosts the phosphor response as it passes, so the glass
    // visibly re-energises rather than sitting at a steady glow. ──
    // sweepSpeed is the whole recharge clock, gating the band AND the
    // persistence breathing at :99, so that its declared minimum of 0 stills
    // the pack. Dropping the iTime term alone does not: sweepPhase becomes a
    // function of position, exp() peaks at diag = 0.714, and the pane keeps a
    // permanently bright diagonal band that only Glow strength 0 could remove,
    // which also removes the effect the pack exists for. Gating rather than
    // scaling the clock leaves the look at every non-zero speed untouched. The
    // 1e-4 edge is step()'s, since step(0.0, 0.0) answers 1.
    float sweepSpeed = max(p_sweepSpeed, 0.0);
    float recharging = step(1e-4, sweepSpeed);
    float sweepPhase = fract(diag * 0.7 - iTime * sweepSpeed) - 0.5;
    float sweep = exp(-sweepPhase * sweepPhase / 0.008) * recharging;

    // Gradient hue flows gently through the pane.
    vec3 glowCol = fluxGradient(pingPong(fuv.x * 0.55 + fuv.y * 0.30 + iTime * max(p_flowSpeed, 0.0)));

    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    float glowStrength = clamp(p_glowStrength, 0.0, 2.0);
    vec3 navy = p_colorTint.rgb;

    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        vec4 raw = surfaceBlurTexel(uv);
        vec4 blurred = surfaceBackdropGrade(raw, p_brightness, p_contrast, p_saturation, p_vibrancy,
                                            p_vibrancyDarkness);

        // Un-premultiplied backdrop luminance drives the excitation, taken from
        // the RAW sample rather than the graded one. The grade exists to tune
        // how the pane LOOKS; reading the excitation off it made Brightness and
        // Contrast double as glow controls, so turning the pane down turned the
        // phosphor response down with it.
        float lumN = raw.a > 0.001 ? luma601(raw.rgb / raw.a) : 0.0;

        // ── Phosphor excitation: bright backdrop charges the glass. The
        // response breathes slowly (persistence), and the sweep both boosts
        // charged regions and faintly lights dim ones as it passes. ──
        // The upper edge is held strictly above the lower one. exciteThreshold
        // DECLARES a max of 1.0, and at that value edge0 == edge1, which the GLSL
        // spec leaves undefined for smoothstep: drivers answer 0, 1 or NaN, so the
        // pack's own top-of-range setting rendered differently per driver. The
        // 1e-4 floor keeps the curve's shape everywhere else identical.
        float exciteLo = clamp(p_exciteThreshold, 0.0, 1.0);
        float excite = pow(smoothstep(exciteLo, max(exciteLo + 1e-4, 1.0), lumN), 1.5);
        // Gated by the same recharge clock, because this term took raw iTime
        // and no setting reached it: flowSpeed 0 stops the hue flow and
        // sweepSpeed 0 stopped the band, and this 1.3 rad/s oscillation ran on
        // through both, leaving this the one pack in the family with no still
        // state. 0.75 is the mean of the breathing, so stilling it lands on the
        // value the pane already averages rather than on a new brightness.
        float persist = 0.75 + 0.25 * recharging * sin(iTime * 1.3 + lumN * 6.0 + diag * 4.0);
        float response = excite * persist * (1.0 + sweep * 0.8) + sweep * 0.06;

        // Base pane: the blurred scene sunk toward the navy brand surface.
        vec3 base = mix(blurred.rgb, navy * blurred.a, tintStrength);

        // Soft multiplicative vignette so the pane reads as a pane.
        float vignette = clamp(1.0 - length((fuv - 0.5) * vec2(0.3, 1.0)) * 0.15, 0.0, 1.0);

        vec3 color = (base + glowCol * response * glowStrength * blurred.a) * vignette;
        // Driver-stable grain, weighted by the backdrop alpha.
        color += (hash13(slab.px) - 0.5) * 2.0 * clamp(p_noiseStrength, 0.0, 0.2) * blurred.a;
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
