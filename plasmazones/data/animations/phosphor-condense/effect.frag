// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Condense — a Phosphor-set open/close candidate built on the
// ember motif (phosphor-motes, and the ember layers of phosphor-flux and
// phosphor-stream): the window condenses out of rising luminous dust. A
// noisy frontier climbs the surface from the bottom; below it the image
// has settled, above it there is nothing yet but ember sparks drifting
// upward, thickest right at the frontier where they are visibly feeding
// the picture. The embers and the frontier rim carry the brand accent
// gradient (cyan #22D3EE → blue #3B82F6 → purple #A855F7 → rose #F43F5E).
// Symmetric — the runtime flips iTime on the close leg, so open condenses
// the window out of the dust and close disperses it back through the same
// pass.
//
// Output is premultiplied alpha (phosphor-bloom's contract): rgb is
// already scaled by alpha, and the ember sparks add their own small
// coverage so they read over whatever is behind the window.

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
    // Amount of the window that has condensed. iTime is per-leg [0,1]
    // progress; the runtime runs it 1->0 on the close leg.
    float presence = clamp(iTime, 0.0, 1.0);

    if (presence <= 0.0) return vec4(0.0);
    if (presence >= 1.0) return surfaceColor(uv);

    vec2 anchor = max(iAnchorSize, vec2(1.0));
    float aspect = anchor.x / anchor.y;

    // ── Condensation frontier: mostly height (the picture settles bottom
    // to top, condensing out of the rising dust) roughened by fBm so the
    // edge reads as dust thickening, not a ruled line. threshold stays in
    // ~[0,1]; presence is expanded past it by the softness so every pixel
    // fully clears its smoothstep at the leg endpoints. ──
    float soft = clamp(p_softness, 0.05, 0.4);
    float grain = clamp(p_grain, 0.2, 3.0);
    float n = fbm(uv * vec2(aspect, 1.0) * grain * 4.0 + 3.1, 4, 2.0);
    float threshold = (1.0 - uv.y) * 0.45 + n * 0.55;

    float slack = soft;
    float pp = presence * (1.0 + 2.0 * slack) - slack;

    // reveal: 0 = still dust, 1 = condensed picture.
    float reveal = smoothstep(-soft, soft, pp - threshold);

    // Mid-leg envelope: exactly zero at both endpoints so the settled and
    // the fully-dispersed window carry no live sparks into the early-outs.
    float mid = clamp(presence * (1.0 - presence) * 4.0, 0.0, 1.0);

    vec4 s = surfaceColor(uv);   // premultiplied
    vec4 col = s * reveal;

    // Height sample of the gradient shared by rim and embers: cyan low,
    // rose at the top, so the whole condensation reads in brand order.
    float yUp = 1.0 - uv.y;

    // ── Frontier rim: the dust glows where it is actively condensing.
    // Coverage-weighted so the light stays inside the window silhouette. ──
    // Squared via multiply: pow(x, 2.0) is undefined for x < 0 per the GLSL
    // spec (see phosphor-stream), and this argument is signed.
    float rimD = (pp - threshold) / (soft * 0.6);
    float rim = exp(-rimD * rimD);
    vec3 rimCol = fluxGradient(clamp(yUp * 0.8 + n * 0.2, 0.0, 1.0));
    float sparkle = 0.85 + 0.30 * niriHash(floor(uv * anchor / 2.0)
                                           + floor(float(iFrame) * 0.2));
    col.rgb += rimCol * rim * mid * s.a * clamp(p_glow, 0.0, 2.0) * 0.8 * sparkle;

    // ── Ember columns above the frontier: one comet-tailed spark per
    // column (the motes life cycle — fade in low, burn out at altitude),
    // rising through the un-condensed region. Progress drives the clock,
    // so the close leg plays the dust in reverse. Emissive with a small
    // coverage bump of its own: the un-condensed region has no surface
    // alpha to ride, and without it the sparks could not composite. ──
    float emberAmt = clamp(p_embers, 0.0, 1.0);
    if (emberAmt > 0.001) {
        float density = clamp(p_density, 8.0, 60.0);
        float colW = 1.0 / density;
        float colIdx = floor(uv.x / colW);
        float cx = (colIdx + 0.5) * colW;
        float seed = niriHash(vec2(colIdx * 7.13 + 0.37, 1.7));

        float rate = 0.5 + 0.7 * seed;
        float altitude = fract(seed * 7.0 - presence * rate);

        // Motes-style wander, kept inside the column.
        float sway = sin(yUp * 7.0 + seed * 6.2831853 + presence * 3.0) * colW * 0.30;
        float dx = (uv.x - (cx + sway)) * aspect;
        float dy = yUp - altitude;   // > 0 above the head

        float cw = colW * aspect;
        float tailLen = 0.06 + 0.06 * seed;
        float head = exp(-dx * dx / (cw * cw * 0.05)) * exp(-dy * dy / 0.0002);
        float tailE = exp(-dx * dx / (cw * cw * 0.02))
                    * (dy < 0.0 ? exp(dy / tailLen) : 0.0);

        // Life envelope plus locality: sparks live in the dust, thickest
        // just above the frontier that is consuming them.
        float life = smoothstep(0.0, 0.12, altitude)
                   * (1.0 - smoothstep(0.70, 0.95, altitude));
        float dust = (1.0 - reveal) * (0.35 + 0.65 * exp(-max(threshold - pp, 0.0) / (soft * 2.0)));

        vec3 emberCol = fluxGradient(clamp(altitude * 0.85 + seed * 0.15, 0.0, 1.0));
        float ember = (head + tailE * 0.5) * life * dust * mid * emberAmt;

        col.rgb += emberCol * ember;
        col.a = min(col.a + ember * 0.5, 1.0);
    }

    col.rgb = clamp(col.rgb, 0.0, 1.0);
    col.a = clamp(col.a, 0.0, 1.0);
    return col;
}
