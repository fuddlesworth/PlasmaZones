// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Stream fragment shader — samples the lane-deformed window
// content (the vertex stage does the pour; see effect.vert) and dresses
// the transit in the Phosphor set's light: a brand-gradient glow riding
// each region while it is between rects, hue offset per lane so the
// separated streams read in different stops of the gradient (cyan #22D3EE
// → blue #3B82F6 → purple #A855F7 → rose #F43F5E), and ember sparks shed
// in the un-settled wake (the phosphor-motes motif). Every addition is
// scaled by e·(1-e), so the settled window and the not-yet-moved window
// are both exactly the plain content — the light exists only in transit.
//
// Old and new content are sampled at the SAME card uv and cross-faded by
// arrival ease (flow's contract), so each shows at its own native aspect.

// .xy = card uv within the destination rect, .z = arrival ease, .w = lane
// seed. Interpolated from effect.vert across the grid.
layout(location = 1) in vec4 vFlow;

#include <noise.glsl>

// uOldWindow + oldColor(): the shared captured-old-frame sampler.
#include <old_content.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose.
vec3 fluxGradient(float t) {
    vec3 cyan   = length(p_colorCyan.rgb)   > 0.01 ? p_colorCyan.rgb   : vec3(0.133, 0.827, 0.933);
    vec3 blue   = length(p_colorBlue.rgb)   > 0.01 ? p_colorBlue.rgb   : vec3(0.231, 0.510, 0.965);
    vec3 purple = length(p_colorPurple.rgb) > 0.01 ? p_colorPurple.rgb : vec3(0.659, 0.333, 0.969);
    vec3 rose   = length(p_colorRose.rgb)   > 0.01 ? p_colorRose.rgb   : vec3(0.957, 0.247, 0.369);
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(cyan, blue, clamp(t, 0.0, 1.0));
    c = mix(c, purple, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, rose, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

vec4 pTransition(vec2 uv, float t) {
    vec2 cuv = vFlow.xy;
    float e = clamp(vFlow.z, 0.0, 1.0);
    float lane = vFlow.w;

    // Feathered window mask in card space — the grid spans the padded
    // decoration canvas (paint_capture.cpp sets it to the layer's canvasGeo,
    // falling back to the frame rect), so cuv runs past [0, 1] into the halo
    // band rather than covering the whole output. The card
    // runs past [0, 1] by the decoration chain's outer margin: that band is
    // the halo the compositor composited into the padded canvas, which
    // surfaceColor() resolves through its layer remap, so the mask includes
    // it and the halo streams with the window. Zero pad reduces to [0, 1].
    vec2 pad = surfacePadRel();
    vec2 fw = max(fwidth(cuv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, cuv + pad), smoothstep(vec2(0.0), fw, 1.0 + pad - cuv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        return vec4(0.0);
    }

    vec4 oldC = oldColor(cuv);     // captured old frame, native aspect
    vec4 newC = surfaceColor(cuv); // live new content, native aspect

    // Cross-fade old -> new as each region arrives. Inputs are
    // premultiplied (KWin FBO storage); a straight mix is correct.
    vec4 col = mix(oldC, newC, e);

    // In-transit envelope: peaks mid-flight, exactly zero at both
    // endpoints, so a settled window carries no residue.
    float mid = e * (1.0 - e) * 4.0;

    // Gradient light riding the stream: hue per lane (the separated
    // streams show different stops) drifting with the ease.
    vec3 flux = fluxGradient(fract(lane * 0.35 + e * 0.45));
    // Squared via multiply: pow(x, 2.0) is undefined for x < 0 per the GLSL
    // spec, and (e - 0.5) is negative for half of every leg.
    float d = (e - 0.5) * 3.2;
    float front = exp(-d * d) * mid;
    // Additive emissive: these deliberately push rgb above alpha, breaking
    // the premultiplied rgb <= a invariant the cross-fade packs keep. That is
    // the intended glow, and the clamp below bounds it.
    col.rgb += flux * front * clamp(p_frontGlow, 0.0, 2.0) * 0.55 * col.a;

    // Ember sparks shed in the un-settled wake: brief hash twinkles that
    // die as the region arrives (the phosphor-motes motif). Emissive over
    // the body only — like the front glow above, both terms are weighted by
    // the surface's own coverage. An unweighted alpha bump would raise
    // alpha where rgb stays near zero (translucent window interiors) and
    // composite dark specks instead of light.
    float sparks = clamp(p_sparks, 0.0, 1.0);
    if (sparks > 0.001) {
        float n = niriHash(floor(cuv * vec2(48.0, 27.0)) + floor(t * 40.0) * 0.37);
        float spark = step(0.94, n) * (1.0 - e) * mid * sparks;
        col.rgb += flux * spark * 1.4 * col.a;
        col.a = min(col.a + spark * 0.2 * col.a, 1.0);
    }

    col.rgb = clamp(col.rgb, 0.0, 1.0);
    return col * mask;
}
