// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Siphon fragment shader — samples the lane-deformed window
// content (the vertex stage does the siphon; see effect.vert) and dresses
// the transit in the Phosphor set's light: a brand-gradient glow riding
// each region while it is in flight, hue offset per lane so the separated
// streams read in different stops of the gradient (cyan #22D3EE → blue
// #3B82F6 → purple #A855F7 → rose #F43F5E), and ember sparks shed in the
// wake (the phosphor-motes motif). Every addition is scaled by e·(1-e),
// so the resting window is exactly the plain content.
//
// Unlike phosphor-stream (whose regions settle INTO the destination and
// stay visible), a siphoned region's destination is the icon: each region
// fades out over the last stretch of its own arrival, so the streams
// vanish into the icon as they land and nothing pops on the teardown
// frame. Restoring runs the ease the other way, so the same ramp fades
// each region in as it pours back out of the icon.

#ifdef PLASMAZONES_KWIN
// .xy = card uv, .z = arrival ease (0 = at rest, 1 = inside the icon),
// .w = lane seed. Interpolated from effect.vert across the grid.
// iIconRect is consumed in the vertex stage.
layout(location = 1) in vec4 vSiphon;
#endif

#include <anchor_remap.glsl>
#include <noise.glsl>

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
#ifdef PLASMAZONES_KWIN
    vec2 cuv = vSiphon.xy;
    float e = clamp(vSiphon.z, 0.0, 1.0);
    float lane = vSiphon.w;

    // Feathered card-edge mask in card space — the grid sits on the
    // window's frame rect (or its padded decoration canvas), so this
    // feathers the edges and crops any halo band past [0, 1].
    vec2 fw = max(fwidth(cuv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, cuv), smoothstep(vec2(0.0), fw, 1.0 - cuv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        return vec4(0.0);
    }

    vec4 col = surfaceColor(cuv);

    // In-transit envelope: peaks mid-flight, exactly zero at both
    // endpoints, so a resting window carries no residue.
    float mid = e * (1.0 - e) * 4.0;

    // Gradient light riding the stream: hue per lane (the separated
    // streams show different stops) drifting with the ease. Squaring is
    // written as a multiply — pow(x, 2.0) is undefined for x < 0 per the
    // GLSL spec and (e - 0.5) is negative for half of every leg (same
    // note as phosphor-stream).
    vec3 flux = fluxGradient(fract(lane * 0.35 + e * 0.45));
    float d = (e - 0.5) * 3.2;
    float front = exp(-d * d) * mid;
    col.rgb += flux * front * clamp(p_frontGlow, 0.0, 2.0) * 0.55 * col.a;

    // Ember sparks shed in the wake: brief hash twinkles that die as the
    // region arrives. Emissive over the body only — both light terms are
    // weighted by the surface's own coverage so translucent interiors
    // never composite dark specks (phosphor-stream's rationale).
    float sparks = clamp(p_sparks, 0.0, 1.0);
    if (sparks > 0.001) {
        float n = niriHash(floor(cuv * vec2(48.0, 27.0)) + floor(t * 40.0) * 0.37);
        float spark = step(0.94, n) * (1.0 - e) * mid * sparks;
        col.rgb += flux * spark * 1.4 * col.a;
        col.a = min(col.a + spark * 0.2 * col.a, 1.0);
    }

    // Each region fades out over the last stretch of its own arrival, so
    // the streams vanish into the icon as they land (and fade back in as
    // they leave it on restore).
    float alpha = 1.0 - smoothstep(0.7, 1.0, e);

    col.rgb = clamp(col.rgb, 0.0, 1.0);
    return col * (mask * alpha);
#else
    // Daemon path: the siphon is compositor-only. Degrade to a plain fade
    // so an assignment to an overlay show/hide leg still animates. The
    // host flips iTime on hide legs, so one expression covers both
    // directions.
    return surfaceColor(anchorRemap(uv)) * clamp(iTime, 0.0, 1.0);
#endif
}
