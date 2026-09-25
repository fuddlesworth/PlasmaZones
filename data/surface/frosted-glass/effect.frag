// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frosted-glass pack, main pass: the phosphor-shell frosted panel shader
// (examples/phosphor-shell/shaders/frosted_glass.frag) ported onto a REAL
// blurred backdrop. The original faked frosting with a translucent tint
// slab; here the slab is the dual-Kawase-blurred scene behind the surface
// (iChannel6), and the original's layers ride on top: the multi-octave
// crystalline Voronoi grain (its finer octaves drift on iTime; the dominant
// one is static), the multiplicative vignette, and the rounded-corner SDF
// clip. NOT unchanged: the original's flat tint is replaced by the turning
// two-colour gradient, which is also what the no-backdrop fallback draws.
//
// SHARED BACKDROP STAGES, in order: the blurred sample runs through
// surfaceBackdropGrade (brightness, contrast, OKLab saturation, vibrancy)
// before the gradient tint, and the Voronoi grain and vignette ride on top
// as the original did.
//
// NO-BACKDROP FALLBACK: when the host bound nothing behind the surface
// (uHasBackdrop = 0), the pack renders the ORIGINAL pseudo look instead,
// the translucent tint slab with the same grain and vignette.
//
// Content dimming: the window sample is dimmed by the pack's own
// p_contentOpacity parameter, so the pane stays solid and translucency
// reveals the frosted backdrop. A theme's own transparent pixels reveal it
// the same way with no parameter involved.

#include <surface_multipass.glsl>
#include <surface_noise.glsl>
#include <surface_color.glsl>

// Animated two-colour gradient, ported from the shell's gradient.frag
// computeGradient(): the direction turns continuously with time and the
// gradient position drifts on two out-of-phase sines, so the colours sweep
// across the pane rather than sitting still.
//
// THE ANGLE IS PANE-RELATIVE, not a screen angle, and that is the upstream
// behaviour rather than a slip. `dir` is dotted against panelUv, which is
// normalised per axis, so the iso-lines are x/W*cos(a) + y/H*sin(a) and the
// device-px gradient direction is (cos(a)/W, sin(a)/H). On a 1600x400 pane a
// declared 45 degrees renders at about 76. Left as the port has it, because a
// pane-relative direction is a coherent meaning for the control (45 degrees is
// corner to corner whatever the window's shape) and aspect-correcting `dir` would
// change how every non-square window looks. The parameter says so now.
vec3 gradientColor(vec2 panelUv) {
    float speed = max(p_gradientSpeed, 0.0);
    float rotatedAngle = radians(p_gradientAngle) + iTime * speed;
    vec2 dir = vec2(cos(rotatedAngle), sin(rotatedAngle));
    float t = dot(panelUv - 0.5, dir) + 0.5;
    // Gated on the speed so 0 really freezes, which is what the parameter
    // DECLARES ("where 0 freezes it") and what its declared minimum of 0 makes
    // reachable. The second sine carries a +1.57 phase offset, so at speed 0 it
    // did not vanish with the first: it settled on sin(1.57) * 0.35, a constant
    // +0.35 bias that pushed the mix factor to [0.282, 1.0] at the default angle.
    // The first colour never rendered and the trailing third of the pane was
    // flat second colour. Every non-zero speed is byte-identical to before.
    if (speed > 0.0) {
        t += sin(iTime * speed * 2.7) * 0.55 + sin(iTime * speed * 1.7 + 1.57) * 0.35;
    }
    t = smoothstep(0.0, 1.0, t);
    return mix(p_colorA.rgb, p_colorB.rgb, t);
}

// Multi-octave crystalline texture: dominant crystal structure + drifting
// fine detail + micro noise.
float frostedTexture(vec2 p, float time) {
    float crystal = voronoi(p);
    float fine = voronoi(p * 2.3 + vec2(time * 0.1, time * 0.07));
    float micro = vnoise(p * 8.0 + time * 0.2);
    return crystal * 0.6 + fine * 0.3 + micro * 0.1;
}

vec4 pSurface(vec2 uv) {
    float cornerPx = p_cornerRadius * uSurfaceScale;
    SurfaceSlab slab = surfaceSlabOpen(uv, cornerPx, p_roundBottomCorners >= 0.5 ? cornerPx : 0.0, p_edgeSoftness);
    // Fade the window content over the pane; the translucency it frees is
    // filled by the frosted backdrop in slabComposite below.
    slab.window *= clamp(p_contentOpacity, 0.0, 1.0);

    // Frame-normalized coordinate for the grain and vignette, so the look
    // scales with the pane like the original's uv did with the panel.
    vec2 fuv = frameUv(slab.px);

    // Crystalline frost variation, centered around zero (both directions —
    // one-sided clamping made the grain read as dark speckles only).
    //
    // Gated on the amount, which DECLARES a minimum of 0. frostedTexture is two
    // voronoi lookups plus a vnoise, and voronoi alone is a 3x3 loop with two
    // hash13 per cell, so the call is about forty hash13 per fragment — the
    // single most expensive thing in this shader. Ungated, a user who turns the
    // grain off paid all of it to multiply the result by zero.
    // NOTE for the no-backdrop fallback below: gradientStrength controls the
    // slab's ALPHA there, not whether the gradient appears. So at its declared
    // minimum of 0 the fallback still draws a full-saturation two-colour
    // gradient, at 40% alpha, while the backdrop path at 0 shows no gradient at
    // all. That asymmetry is deliberate — the fallback has nothing else to
    // draw, and a fully transparent pane would communicate nothing — but it is
    // not what the parameter's description says, so do not read the two paths
    // as one control.
    float variation = 0.0;
    if (p_grainAmount > 0.0) {
        // ASPECT-CORRECTED, because voronoi works on an isotropic unit lattice and
        // fuv is frameUv, which normalises each axis SEPARATELY. Feeding it a
        // scalar scale made the cell lattice inherit the pane's aspect, so on a
        // 1600x400 pane the "crystals" rendered as 4:1 ellipses, which no reading
        // of "Density of the frost crystals" describes. The same normalisation also
        // tied crystal SIZE to window size, so one setting looked different on
        // every window. Dividing BOTH axes by the SHORT side is what fixes that: the
        // cell then measures shortSide/grainScale device px whichever way the pane is
        // oriented, and the long axis simply gets proportionally more cells, which is
        // what a density means. Dividing by the height alone made the cells square but
        // left their size tracking one axis, so a 1600x400 pane and its transpose
        // still disagreed by a factor of four at the same setting.
        float grainShort = max(min(uSurfaceFrameSize.x, uSurfaceFrameSize.y), 1.0);
        vec2 grainAspect = uSurfaceFrameSize / grainShort;
        float frost = frostedTexture(fuv * grainAspect * p_grainScale, iTime * p_grainSpeed);
        variation = (frost - 0.5) * p_grainAmount;
    }

    // Vignette darkens edges, multiplicative so it never brightens.
    //
    // The vec2(0.3, 1.0) weight is ASYMMETRIC and comes verbatim from the shell
    // panel shader this pack ports, which was written for one wide, short
    // TopPanel. It makes the vertical falloff 3.3x the horizontal, so at the
    // declared max of 0.5 the mid-left and mid-right edges darken about 7.5%
    // against 25% at top and bottom: this reads as a vertical gradient, not a
    // vignette. Kept, because changing it would restyle every pane that ships
    // with the pack; the parameter's description says top and bottom rather
    // than "edges" so the control does not promise the symmetric thing.
    float vignette = clamp(1.0 - length((fuv - 0.5) * vec2(0.3, 1.0)) * p_vignetteStrength, 0.0, 1.0);

    vec3 grad = gradientColor(fuv);
    float gradStrength = clamp(p_gradientStrength, 0.0, 1.0);
    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Real frosting: blurred backdrop tinted by the turning gradient,
        // grained, vignetted.
        vec4 blurred = surfaceBackdropGrade(surfaceBlurTexel(uv), p_brightness, p_contrast, p_saturation,
                                            p_vibrancy, p_vibrancyDarkness);
        vec3 color = mix(blurred.rgb, grad * blurred.a, gradStrength);
        color = clamp(color + vec3(variation) * blurred.a, 0.0, max(blurred.a, 0.0001));
        color *= vignette;
        pane = vec4(color, blurred.a) * slab.mask;
    } else {
        // Original pseudo look (the shell TopPanel): a translucent animated
        // gradient slab with the same grain and vignette.
        float slabAlpha = clamp(0.4 + 0.6 * gradStrength, 0.0, 1.0);
        vec3 color = clamp(grad + vec3(variation), 0.0, 1.0) * vignette;
        pane = vec4(color, 1.0) * slabAlpha * slab.mask;
    }

    return slabComposite(slab.window, pane);
}
