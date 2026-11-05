// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Ignition — a Phosphor-set open/close candidate built on the
// plasma motif (phosphor-vortex's element, minus the vortex): the window
// ignites from its centre. A plasma bloom expands radially behind a
// turbulent, noise-eaten rim of brand light with soft tendrils reaching
// ahead of it, the freshly-passed region shimmers briefly with heat, and
// the rim carries the brand accent gradient (cyan #22D3EE → blue #3B82F6
// → purple #A855F7 → rose #F43F5E) from cyan at the core to rose at the
// corners. Outside the bloom there is nothing yet — the window literally
// grows out of the ignition.
// Symmetric — the runtime flips iTime on the close leg, so open ignites
// outward and close collapses the plasma back into the centre through the
// same pass.
//
// Output is premultiplied alpha (phosphor-bloom's contract): rgb is
// already scaled by alpha, plus an additive rim glow bounded by the
// surface coverage.

#include <animation_uniforms.glsl>
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

vec4 pTransition(vec2 uv, float t)
{
    // Radius of the ignition. iTime is per-leg [0,1] progress; the runtime
    // runs it 1->0 on the close leg, collapsing the plasma back in.
    float presence = clamp(iTime, 0.0, 1.0);

    if (presence <= 0.0) return vec4(0.0);
    if (presence >= 1.0) return surfaceColor(uv);

    vec2 anchor = max(iAnchorSize, vec2(1.0));
    float aspect = anchor.x / anchor.y;

    // ── Aspect-corrected radial coordinate: rr is 0 at the window centre
    // and 1 at the corners, so a square bloom covers any window shape. ──
    vec2 pos = (uv - 0.5) * vec2(aspect, 1.0);
    float maxR = 0.5 * length(vec2(aspect, 1.0));
    float rr = length(pos) / maxR;

    // ── Local ignition threshold: the radius roughened by fBm (the rim is
    // eaten by turbulence, never a compass circle) and by angular tendrils
    // reaching ahead of the bloom. Both distortions are bounded so the
    // slack below can guarantee clean endpoints. ──
    float soft = clamp(p_softness, 0.04, 0.3);
    float turb = clamp(p_turbulence, 0.0, 1.0);
    float arms = clamp(p_arms, 0.0, 9.0);

    float n = fbm(pos * 4.0 / max(maxR, 1.0e-4) * 0.5 + 7.3, 4, 2.0);
    float tendril = 0.0;
    if (arms >= 0.5) {
        float ang = atan(pos.y, pos.x);
        // floor(arms + 0.5) keeps the petal count integral so the tendril
        // field is continuous across the atan seam at ±π.
        float armN = floor(arms + 0.5);
        tendril = (0.5 + 0.5 * sin(ang * armN + rr * 5.0 + presence * 3.0)) * 0.5;
    }
    float wobble = (n - 0.5) * turb * 0.30 + tendril * turb * 0.12;
    float threshold = rr + wobble;

    // Expand presence past [0,1] by the softness plus the worst-case
    // wobble, with the same 3x gaussian margin as phosphor-lock so the rim
    // glow (not just the reveal smoothstep) is fully off at both leg
    // endpoints.
    float slack = 3.0 * soft + 0.21 * turb;
    float front = presence * (1.0 + 2.0 * slack) - slack;

    // m > 0: the bloom has passed this pixel.
    float m = front - threshold;

    // reveal: 0 outside the plasma, 1 behind it.
    float reveal = smoothstep(0.0, soft, m);

    // ── Heat shimmer: the freshly-ignited region refracts outward for a
    // moment, strongest just behind the rim. Boundary-masked so the
    // displaced sample never smears the clamped edge texel. Squared via
    // multiply — pow(x, 2) is undefined for x < 0 per the GLSL spec and m
    // is signed. ──
    float md = m / soft;
    float rim = exp(-md * md);
    vec2 outward = length(pos) > 1.0e-4 ? pos / length(pos) : vec2(0.0);
    vec2 bend = outward * rim * clamp(p_shimmer, 0.0, 1.0) * 0.015 / vec2(aspect, 1.0);
    vec2 suv = uv - bend;
    vec4 s = surfaceColor(suv) * boundaryMask(suv);   // premultiplied

    vec4 col = s * reveal;

    // ── Plasma rim: the gradient rides the radius (cyan at the core, rose
    // at the corners), lifted by the tendrils and a fine per-frame grain.
    // Coverage-weighted so the light stays inside the window silhouette. ──
    vec3 rimCol = fluxGradient(clamp(rr * 1.1, 0.0, 1.0));
    float sparkle = 0.85 + 0.30 * niriHash(floor(uv * anchor / 2.0)
                                           + floor(float(iFrame) * 0.2));
    float lift = 0.7 + 0.6 * tendril;
    col.rgb += rimCol * rim * lift * s.a * clamp(p_glow, 0.0, 2.0) * sparkle;

    // Same bounds as phosphor-bloom: bound the additive emissive here, and
    // never let a pixel be more opaque than the window is at that point.
    col.rgb = clamp(col.rgb, 0.0, 1.0);
    col.a = clamp(col.a + rim * s.a * 0.4, 0.0, s.a);

    return col;
}
