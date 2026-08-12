// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Swap — the tab leg of the branded set, and the class the family did
// not cover yet. phosphor-gate pins two energize fields at the screen edges and
// lets the strip flow through them. A tab swap has no motion at all, so the
// same motif is inverted: ONE gate travels across the column and the content is
// what stands still. Ahead of the travelling boundary the outgoing tab drains
// into the family's dark navy silhouette. Behind it the arriving tab climbs
// back out of that silhouette into full colour, with a rim of brand light
// riding the boundary itself, ember sparks drifting into that rim from the
// unlit side, and a rose afterglow left in the boundary's wake.
//
// The changeover itself goes through oldCrossFade, so the pack never invents
// which side is the from and which the to — that is a fact about the event,
// carried by old_content.glsl.
//
// Tab-only pack, hence compositor-only (shaderEffectIsCompositorOnly): the
// daemon never bakes it, so old_content.glsl's binding-less uOldWindow
// declaration needs no PLASMAZONES_KWIN guard.
//
// Nothing here displaces a sample. Both taps are at the fragment's own uv,
// always inside [0, 1], so there is no clamp-to-edge smear to mask and no
// boundaryMask call — the guard the zoom-style packs need does not apply.
//
// Geometry and texture coordinates coincide, so the samplers take uv directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl carries uOldWindow / oldColor / oldCrossFade;
// noise.glsl carries fbm (the boundary meander) and niriHash (the sparks and
// the rim grain).
#include <old_content.glsl>
#include <noise.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose. Every
// pack in the set carries its own copy of this helper rather than sharing one,
// so it is lifted verbatim from phosphor-condense.
//
// The family's unset-probe is length() > 0.01: a customColors slot the profile
// never set arrives as all-zero, and that is the only thing distinguishing it
// from a chosen colour. The corollary is family-wide and deliberate, a user who
// picks a NEAR-BLACK colour gets the bright default substituted instead.
vec3 fluxGradient(float g) {
    vec3 cyan   = length(p_colorCyan.rgb)   > 0.01 ? p_colorCyan.rgb   : vec3(0.133, 0.827, 0.933);
    vec3 blue   = length(p_colorBlue.rgb)   > 0.01 ? p_colorBlue.rgb   : vec3(0.231, 0.510, 0.965);
    vec3 purple = length(p_colorPurple.rgb) > 0.01 ? p_colorPurple.rgb : vec3(0.659, 0.333, 0.969);
    vec3 rose   = length(p_colorRose.rgb)   > 0.01 ? p_colorRose.rgb   : vec3(0.957, 0.247, 0.369);
    g = clamp(g, 0.0, 1.0) * 3.0;
    vec3 c = mix(cyan, blue, clamp(g, 0.0, 1.0));
    c = mix(c, purple, clamp(g - 1.0, 0.0, 1.0));
    c = mix(c, rose, clamp(g - 2.0, 0.0, 1.0));
    return c;
}

vec4 pTransition(vec2 uv, float t) {
    // Clamped for tab-fade's reason: a tab swap has no visual meaning outside
    // its endpoints, and an overshooting curve would otherwise push the
    // boundary and the gradient weights past the range every term below
    // assumes.
    float p = clamp(t, 0.0, 1.0);

    // Zero-direction guard: fall back to a rightward sweep rather than
    // epsilon-nudging into normalize, where two tiny opposing components can
    // still cancel and produce a NaN (tab-wipe's idiom).
    vec2 raw = vec2(p_dirX, p_dirY);
    vec2 dir = dot(raw, raw) > 1.0e-6 ? normalize(raw) : vec2(1.0, 0.0);

    // Projection normalised by the direction's L1 extent so proj spans exactly
    // [0, 1] corner to corner for ANY direction. A plain dot() + 0.5 overshoots
    // on diagonals (a unit diagonal reaches ±0.707), which would leave a corner
    // sliver untransitioned at both ends. ext is >= 1 for a unit vector, so it
    // can never divide by zero, and it reduces to uv.x for (1, 0).
    float ext = abs(dir.x) + abs(dir.y);
    float proj = dot(uv - vec2(0.5), dir) / ext + 0.5;
    // The coordinate ALONG the boundary. The meander varies over it and the
    // sparks are laned by it. The perpendicular of a unit vector has the same
    // L1 extent, so the same divisor normalises it and both stay
    // direction-independent.
    float lateral = dot(uv - vec2(0.5), vec2(-dir.y, dir.x)) / ext + 0.5;

    float soft = max(p_softness, 1.0e-3);
    float depth = max(p_depth, 1.0e-3);
    float meander = clamp(p_meander, 0.0, 1.0);

    // Meander amplitude is measured as a fraction of the gate DEPTH, not of the
    // rect. Anchoring it to the rect (phosphor-gate scales by its field depth,
    // which is the same idea in that pack's units) made the waver dwarf the
    // unlit band at small depths, and the boundary stopped reading as one
    // travelling front. fbm's three octaves sum to 0.875, not 1, so normalise
    // before centring or the meander sits biased toward one side.
    float mAmp = 0.35 * depth * meander;
    float m = (fbm(vec2(lateral * 3.0 * max(p_grain, 0.2), 0.7 + p * 0.6), 3, 2.0) / 0.875 - 0.5)
        * 2.0 * mAmp;

    // Two mechanisms keep the endpoints clean, and neither is sufficient alone.
    //
    // Padding moves the boundary fully off the rect before p reaches either
    // end, so no fragment is caught mid-changeover at t = 0 or t = 1. Padding
    // by the FULL reach of every term (the unlit band, the sparks, and
    // especially the rose afterglow, which is exponential and has no hard end)
    // would need a pad near 0.5, which spends a third of the leg with the sweep
    // parked off-screen doing nothing. So the pad covers the boundary geometry
    // and half the band.
    //
    // The envelope closes the rest: it is exactly 0 at both endpoints, so the
    // frame the user lands on is bit-exact oldColor / surfaceColor, and it
    // ramps over the first and last 18% of the leg, which is where the residual
    // half-band and the afterglow live. Read together the wake does not stop,
    // it fades, which is what a wake should do anyway.
    float pad = soft + mAmp + 0.5 * depth;
    float sweep = p * (1.0 + 2.0 * pad) - pad + m;
    float env = smoothstep(0.0, 0.18, p) * (1.0 - smoothstep(0.82, 1.0, p));

    // Signed distance to the boundary: positive is AHEAD of it (still the
    // outgoing tab), negative is BEHIND (already the arriving one).
    float d = proj - sweep;

    // The changeover. Behind the boundary the fragment has fully turned over.
    // smoothstep needs edge0 < edge1, which the max() on soft guarantees.
    vec4 base = oldCrossFade(uv, 1.0 - smoothstep(-soft, soft, d));
    vec3 col = base.rgb;

    if (env <= 0.0) {
        return base; // endpoint identity, made explicit
    }

    // Content is PREMULTIPLIED. Both tabs are opaque so base.a is 1 in
    // practice, but every added or substituted colour below is scaled by base.a
    // anyway: on a rounded corner or a shadow texel where alpha is genuinely
    // below 1, an unscaled tint would paint colour into a region with no
    // coverage and the silhouette would grow a fringe outside the window.
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    vec3 tint = length(p_colorTint.rgb) > 0.01 ? p_colorTint.rgb : vec3(0.043, 0.090, 0.188);
    // The family silhouette: dark navy shaped by the content's own luminance,
    // so a bright window darkens to a readable ghost rather than a flat plate.
    // This is a TINT over content, never a coverage cut — dropping alpha here
    // would show the wallpaper straight through the column, which is the one
    // failure a tab swap can never have.
    vec3 sil = tint * (0.5 + 1.6 * lum) * base.a;

    float glow = clamp(p_glow, 0.0, 2.0);
    float dimK = clamp(p_unlitDim, 0.0, 1.0);
    float lumW = 0.35 + 0.65 * lum;

    // ── Unlit band: draining ahead, coming back up behind ──
    // Both terms reach exactly 1 at d = 0, so the two halves meet at the
    // boundary with no seam. The trailing side reaches further because coming
    // back up should take longer than going down, and that asymmetry is what
    // makes the boundary read as travelling rather than as a symmetric stripe.
    float drain = 1.0 - smoothstep(0.0, depth * 0.8, max(d, 0.0));
    float rise = 1.0 - smoothstep(0.0, depth * 1.3, max(-d, 0.0));
    col = mix(col, sil, max(drain, rise) * dimK * env);

    // ── Rim of brand light on the boundary itself ──
    // Squared via multiply: pow(x, 2.0) is undefined for x < 0 per the GLSL
    // spec and rimD is signed. The gradient stop walks with the leg, so the rim
    // leads cyan and grades toward purple as the swap completes, and it rides
    // the content's luminance so it does not float over black areas.
    float rimW = max(soft * 0.9, 0.01);
    float rimD = d / rimW;
    float rim = exp(-rimD * rimD);
    // The family's grain, which keeps the rim from reading as a clean gradient
    // ramp. iAnchorSize, not iResolution: Qt resets iResolution to the
    // QQuickItem's bounds, and the anchor is the card's real pixel size. The
    // max() covers the early-frame zero-size case, where every cell would
    // otherwise collapse onto one hash input and the grain would flicker the
    // whole rect as a single block.
    vec2 anchor = max(iAnchorSize, vec2(1.0));
    float sparkle = 0.85 + 0.30 * niriHash(floor(uv * anchor / 2.0) + floor(float(iFrame) * 0.2));
    col += fluxGradient(clamp(0.05 + p * 0.55, 0.0, 1.0)) * rim * glow * sparkle * lumW * base.a * env
        * 0.9;

    // ── Rose afterglow in the boundary's wake ──
    // Exponential in the same projection units the band uses, so the slider
    // lengthens the wake instead of only brightening it. Only the trailing side
    // carries it, because an afterglow is what content leaves once it has
    // changed over, and the length is floored away from zero so the divide is
    // safe at the bottom of the slider's range.
    float wake = max(0.04 + 0.22 * clamp(p_afterglow, 0.0, 1.0), 1.0e-3);
    float trailGlow = d < 0.0 ? exp(d / wake) : 0.0;
    col += fluxGradient(clamp(0.72 + 0.28 * (1.0 - p), 0.72, 1.0)) * trailGlow * glow * lumW * base.a
        * env * 0.35;

    // ── Ember sparks feeding the rim from the unlit side ──
    // phosphor-gate's comets, re-laned: there the sparks drift inward from a
    // pinned screen edge, here they drift down the gate depth toward a boundary
    // that is itself moving, and are consumed at the rim. Progress drives the
    // per-lane clock, since a tab leg has no seconds uniform to key off and iTime
    // IS the progress for this class.
    float embers = clamp(p_embers, 0.0, 1.0);
    if (embers > 0.001) {
        float lanes = clamp(p_density, 8.0, 60.0);
        float yl = lateral * lanes;
        float lane = floor(yl);
        float within = yl - lane;
        float seed = niriHash(vec2(lane * 7.13 + 0.37, 1.7));

        float ph = fract(seed * 7.0 + p * (1.5 + 2.0 * seed));
        float eq = ph * depth * 1.1;   // the spark's own distance ahead of the boundary
        float dq = d - eq;             // 0 at the head, positive further out in the unlit band

        // Each spark lives entirely inside its own lane: the jittered centre is
        // held away from the lane boundaries and the lateral falloff is
        // windowed to zero AT those boundaries. Without the window a spark
        // whose centre sat near an edge stayed bright right up to it and then
        // dropped to the neighbouring lane's unrelated value, drawing a visible
        // seam along every lane (gate hit this and the fix is the same here).
        float cy = 0.35 + 0.30 * niriHash(vec2(lane, 9.1));
        float dy = within - cy;
        float lat = exp(-dy * dy * 10.0) * (1.0 - smoothstep(0.30, 0.5, abs(within - 0.5)));

        // Head and trail are sized in units of the gate depth rather than in
        // absolute uv, so a shallow gate gets small sparks instead of blobs
        // that swamp their own band. depth is floored above, so both divides
        // are safe.
        float head = exp(-dq * dq / (depth * depth * 0.01)) * lat;
        float trail = dq > 0.0 ? exp(-dq / (depth * 0.35)) * lat * 0.6 : 0.0;

        float life = smoothstep(0.0, 0.15, ph) * (1.0 - smoothstep(0.75, 1.0, ph));
        // Locality: sparks belong to the unlit band ahead of the boundary and
        // are thickest right where the rim is consuming them. `drain` already
        // falls to zero past the band, and the one-sided gate keeps them off
        // the side that has finished changing over.
        float aheadOnly = smoothstep(-soft, soft, d);
        float dust = 0.35 + 0.65 * exp(-max(d, 0.0) / (depth * 0.6));
        col += fluxGradient(clamp(0.10 + eq / max(depth * 1.1, 1.0e-3) * 0.35, 0.0, 1.0))
            * (head + trail) * life * dust * drain * aheadOnly * embers * glow * base.a * env * 1.2;
    }

    // Premultiplied output must satisfy rgb <= a. The additive rim, wake and
    // sparks can push past that on bright content, and a premultiplied fragment
    // with rgb above alpha blends brighter than opaque over whatever is behind
    // the column, which shows up as a glowing fringe on the corner texels
    // rather than as a highlight. Clamping to base.a is the invariant, not a
    // taste call — the desktop and strip packs' no-upper-clamp HDR rationale
    // does not transfer, because those composite a full-coverage capture with
    // no alpha to violate.
    return vec4(clamp(col, vec3(0.0), vec3(max(base.a, 0.0))), base.a);
}
