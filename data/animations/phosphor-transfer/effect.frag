// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Transfer — the Phosphor set's take on `scrolling.tabSwitch`, built
// on the same ember motif as phosphor-condense / phosphor-stream. A ragged
// frontier climbs the column; ahead of it the outgoing tab still holds, behind
// it the arriving tab has settled, and along it the material comes apart into
// luminous dust that re-forms as the other picture. The reading we are after is
// ONE body of glowing material being re-shaped into different content, which is
// why the embers are thickest exactly at the frontier (where the handover is
// happening) and why they can take part of their colour from the tab they broke
// off (see p_carry) rather than always being pure gradient.
//
// Why a frontier at all, when nothing moves in a tab swap? Because the two
// endpoints are two unrelated pictures at the same rect, and a whole-surface
// dissolve gives the dust nowhere to come FROM. Localising the breakup to a
// travelling band gives the sparks an origin and a destination, so the pass
// reads as a transfer instead of a fade with particles sprinkled on top. The
// climb direction matches phosphor-condense so the set stays coherent.
//
// uOldWindow holds the OUTGOING tab, not this window's own earlier content —
// the one place the shared old-content sampler is fed from a different window
// (see tab-fade's header for the coordinate-system argument). Tab-only pack,
// hence compositor-only, so old_content.glsl's binding-less uOldWindow
// declaration needs no PLASMAZONES_KWIN guard.
//
// Output is premultiplied alpha. Both tabs are opaque, so the base blend's
// weights are kept summing to exactly 1 and the breakup dim touches rgb ONLY —
// dimming premultiplied alpha instead would punch a hole in the column and show
// the wallpaper through it for those frames, which is the failure this pack is
// most likely to hit if someone "simplifies" the dim later. The emissive sparks
// add a little coverage of their own on top, because a tab with rounded corners
// or a transparent client area has no surface alpha there for them to ride.
//
// Geometry and texture coordinates coincide and nothing here samples outside
// [0, 1], so no boundaryMask is needed — every sample is uv itself.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl carries uOldWindow / oldColor; noise.glsl carries
// fbm / niriHash.
#include <old_content.glsl>
#include <noise.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose. Same
// helper phosphor-condense and phosphor-gate carry, including the non-black
// fallback — a colour parameter that never resolved reads back as vec4(0), and
// without the length() test the whole ember layer would silently render black.
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
    // Clamped for the reason tab-fade documents: a tab swap has no meaning
    // outside its endpoints, and an overshooting curve would otherwise drive
    // one side of the blend past 1 and the other below 0, which on
    // premultiplied content reads as the arriving tab flaring over-bright while
    // the departing one goes inverted.
    float p = clamp(t, 0.0, 1.0);

    // Endpoint early-outs. The body below already resolves to exactly these two
    // (the padding makes `handed` reach 0 and 1, and `mid` is zero at both
    // ends), so this is a cost saving rather than a correctness fix — it keeps
    // the first and last frame of every tab swap down to a single fetch.
    if (p <= 0.0) return oldColor(uv);
    if (p >= 1.0) return surfaceColor(uv);

    vec2 anchor = max(iAnchorSize, vec2(1.0));
    // Clamped: a pathologically tall, narrow tab column would otherwise drive
    // the aspect toward zero and the ember-column math into denormal territory.
    // The head/tail Gaussians below are aspect-invariant by construction (dx
    // and cw both scale with it, so it cancels), but the frontier's noise
    // lookup is not, and an unbounded aspect there smears the grain into
    // stripes.
    float aspect = clamp(anchor.x / anchor.y, 0.05, 20.0);

    // ── Transfer frontier. Mostly height, so the handover climbs the column,
    // roughened by fBm so the boundary reads as material tearing rather than a
    // ruled wipe. threshold stays in ~[0, 1]. ──
    float soft = clamp(p_softness, 0.05, 0.4);
    float grain = clamp(p_grain, 0.2, 3.0);
    float n = fbm(uv * vec2(aspect, 1.0) * grain * 4.0 + 3.1, 4, 2.0);
    float threshold = (1.0 - uv.y) * 0.45 + n * 0.55;

    // Progress padded by the softness, the tab-dissolve / phosphor-condense
    // idiom. Without it the fragments whose threshold lands within `soft` of
    // either end are still part-blended at p = 0 and p = 1, and the swap starts
    // and finishes on a visible step.
    float pp = p * (1.0 + 2.0 * soft) - soft;
    float d = pp - threshold;

    // 0 = still the outgoing tab, 1 = the arriving tab has settled.
    float handed = smoothstep(-soft, soft, d);

    // oldCrossFade() spelled out, because the ember tint below wants the
    // outgoing sample too and a second oldColor() call would be a second fetch
    // of the same texel. mix() keeps the two weights summing to exactly 1 at
    // every p, which is the property that stops the column going see-through
    // mid-swap.
    vec4 oldC = oldColor(uv);
    vec4 newC = surfaceColor(uv);
    vec4 base = mix(oldC, newC, handed);

    // Gaussian centred on the frontier, in the same units as `soft`. Everything
    // that only happens during the handover (the breakup dim, the rim, the
    // ember locality) is weighted by this, so the pack costs nothing visually
    // in the regions that have already been decided.
    float bandD = d / max(soft, 1.0e-3);
    float band = exp(-bandD * bandD * 1.2);

    // Mid-leg envelope, exactly zero at both endpoints, so no live spark or rim
    // survives into the settled frames.
    float mid = clamp(p * (1.0 - p) * 4.0, 0.0, 1.0);

    // ── Unlit band. Inside the handover the material belongs to neither tab,
    // so it sinks toward the family's dark navy (the same unlit tint
    // phosphor-gate and phosphor-bloom sink their not-yet-lit regions to)
    // rather than toward black, which would read as a hole rather than as
    // unlit material.
    //
    // rgb only, alpha untouched: on premultiplied content that is a darkening
    // of the picture, not a loss of coverage. The tint is multiplied by base.a
    // to enter premultiplied space before the mix, so a partly transparent tab
    // does not gain opaque navy where it had none. The 0.85 ceiling keeps a
    // trace of the underlying image even at unlitDim = 1, so the frontier never
    // becomes a flat navy bar dragged across the tab. ──
    vec3 tint = length(p_colorTint.rgb) > 0.01 ? p_colorTint.rgb : vec3(0.043, 0.090, 0.188);
    float unlit = clamp(p_unlitDim, 0.0, 1.0) * band * mid * 0.85;
    vec4 col = base;
    col.rgb = mix(base.rgb, tint * base.a, unlit);

    // ── Frontier rim. Tighter than the breakup band so the light sits inside
    // the darkened region rather than washing over it. Weighted by base.a so
    // the glow stays within the tab's own silhouette (a rounded corner must not
    // sprout a lit edge). ──
    // Squared by multiplying: pow(x, 2.0) is undefined for x < 0 per the GLSL
    // spec (phosphor-stream hit this), and rimD is signed.
    float rimD = d / max(soft * 0.6, 1.0e-3);
    float rim = exp(-rimD * rimD);
    float yUp = 1.0 - uv.y;
    vec3 rimCol = fluxGradient(clamp(yUp * 0.8 + n * 0.2, 0.0, 1.0));
    // Per-frame twinkle on a 2-device-pixel grid, so the rim shimmers instead
    // of sitting there as a static gradient stripe.
    float sparkle = 0.85 + 0.30 * niriHash(floor(uv * anchor / 2.0)
                                           + floor(float(iFrame) * 0.2));
    col.rgb += rimCol * rim * mid * base.a * clamp(p_glow, 0.0, 2.0) * 0.8 * sparkle;

    // ── Ember columns. One comet-tailed spark per column, rising over the leg,
    // alive only near the frontier — so each spark reads as a piece that broke
    // off the outgoing tab just ahead of the boundary and settled into the
    // arriving one just behind it. Emissive, with a small coverage bump of its
    // own: a spark over a transparent part of the tab has no surface alpha to
    // ride and could not composite without it. ──
    float emberAmt = clamp(p_embers, 0.0, 1.0);
    if (emberAmt > 0.001) {
        float density = clamp(p_density, 8.0, 60.0);
        float colW = 1.0 / density;
        float colIdx = floor(uv.x / colW);
        float cx = (colIdx + 0.5) * colW;
        float seed = niriHash(vec2(colIdx * 7.13 + 0.37, 1.7));

        // Rising: altitude increases with p, unlike phosphor-condense's
        // descending head. There the dust feeds a surface that is condensing
        // out of it; here the dust is carrying material upward across a
        // frontier that is itself climbing. Sparks drifting against that
        // frontier read as debris falling out of the tab, which is the wrong
        // story for a transfer.
        float rate = 0.5 + 0.7 * seed;
        float altitude = fract(seed * 7.0 + p * rate * 1.5);

        // Wander, kept inside the spark's own column so the columns never
        // cross and the density parameter keeps meaning what it says.
        float sway = sin(yUp * 7.0 + seed * 6.2831853 + p * 3.0) * colW * 0.30;
        float dx = (uv.x - (cx + sway)) * aspect;
        float dy = yUp - altitude;   // < 0 below the head, where the tail sits

        float cw = colW * aspect;
        float tailLen = 0.06 + 0.06 * seed;
        // Aspect cancels in both ratios (dx and cw each carry one factor), so
        // these stay round dots on any tab shape. The 1.0e-12 floors are pure
        // defence: colW >= 1/60 and aspect >= 0.05 by the clamps above, so the
        // denominators cannot actually reach zero.
        float cwSq = max(cw * cw, 1.0e-12);
        float head = exp(-dx * dx / max(cwSq * 0.05, 1.0e-12))
                   * exp(-dy * dy / 0.0002);
        float tailE = exp(-dx * dx / max(cwSq * 0.02, 1.0e-12))
                    * (dy < 0.0 ? exp(dy / tailLen) : 0.0);

        // Fade in low, burn out at altitude — the motes life cycle, so sparks
        // never pop into or out of existence at the column ends.
        float life = smoothstep(0.0, 0.12, altitude)
                   * (1.0 - smoothstep(0.70, 0.95, altitude));

        vec3 emberCol = fluxGradient(clamp(altitude * 0.85 + seed * 0.15, 0.0, 1.0));

        // ── Colour carry. Tinting the spark by the outgoing tab's own colour
        // is what sells "the same material re-formed" over "particles drawn on
        // top of a fade". Divide out the premultiply first, then normalise by
        // the largest channel so we take the HUE and not the brightness: a dark
        // terminal tab would otherwise contribute a near-black multiplier and
        // erase its own embers. The 0.15 floor is what makes that safe, and it
        // doubles as the divide guard. ──
        float carry = clamp(p_carry, 0.0, 1.0);
        if (carry > 0.001) {
            vec3 srcRgb = oldC.rgb / max(oldC.a, 1.0e-3);
            float mx = max(max(srcRgb.r, srcRgb.g), max(srcRgb.b, 0.15));
            emberCol = mix(emberCol, emberCol * (srcRgb / mx), carry);
        }

        float ember = (head + tailE * 0.5) * life * band * mid * emberAmt;
        col.rgb += emberCol * ember;
        col.a = min(col.a + ember * 0.5, 1.0);
    }

    col.rgb = clamp(col.rgb, 0.0, 1.0);
    col.a = clamp(col.a, 0.0, 1.0);
    return col;
}
