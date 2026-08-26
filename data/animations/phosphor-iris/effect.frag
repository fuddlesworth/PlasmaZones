// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Iris — the Phosphor set's tab pack, built on the plasma motif that
// phosphor-ignite and phosphor-vortex share, turned into an iris that opens
// between two windows rather than out of nothing.
//
// A ring of plasma opens from the centre of the column. Inside it the incoming
// tab has already formed; outside it the outgoing tab is still there but drains
// toward the dark navy silhouette (the phosphor-gate / phosphor-peek de-energize
// treatment) as the ring closes on it. The ring is the bright event: a
// turbulent, noise-eaten rim carrying the brand accent gradient (cyan #22D3EE →
// blue #3B82F6 → purple #A855F7 → rose #F43F5E) from cyan at the core to rose
// at the corners, with soft tendrils reaching ahead into the region that has
// not changed yet and ember sparks shed in the wake behind it (the
// phosphor-motes motif, as phosphor-stream sheds them). By the time the ring
// clears the corners the swap is done and nothing glows.
//
// ── Why this pack does not need a backdrop fill ─────────────────────────────
// The tab class is the one transition whose two endpoints are BOTH real, live
// window content at the SAME rect: the outgoing tab parks off-canvas and the
// incoming one takes the rectangle it vacated. Nothing here moves. That means
// every pixel always has a picture behind it — inside the iris the arriving
// tab, outside it the (drained) departing one — so the composite is a mix()
// between two opaque premultiplied sources and coverage is preserved by
// construction at every p. There is never a frame where the wallpaper could
// show through the column, and consequently no tint fill is needed to hide one.
// The silhouette is a colour treatment of content that is still there, NOT a
// stand-in for content that is missing.
//
// uOldWindow holds the OUTGOING tab, not this window's own earlier content.
// The tab class is the one place the shared old-content sampler is fed from a
// different window; oldColor()'s fold and Y-flip still apply unchanged, because
// the capture is taken over the arriving window's expanded rect translated to
// the outgoing tab's position, so the two share a frame-relative coordinate
// system.
//
// Tab-only pack, hence compositor-only (shaderEffectIsCompositorOnly): the
// daemon never bakes it, so the binding-less uOldWindow declaration in
// old_content.glsl needs no PLASMAZONES_KWIN guard.
//
// Geometry and texture coordinates coincide, so the samplers take uv directly.
//
// Output is premultiplied alpha (phosphor-bloom's contract): rgb is already
// scaled by alpha, plus an additive rim glow bounded by the surface coverage.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl carries uOldWindow / oldColor; noise.glsl carries
// fbm (the rim turbulence), niriHash (sparkle grain and ember twinkles) and
// boundaryMask (crops the shimmer-displaced samples).
#include <old_content.glsl>
#include <noise.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose. Carried
// per pack across the whole Phosphor family rather than shared, so this is a
// verbatim copy of phosphor-condense's. The non-black fallback per stop is the
// point of the length() test: a user who clears a swatch to pure black would
// otherwise silently delete that stop from the gradient.
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
    // Clamped, unlike the geometry packs': there is no rect to extrapolate
    // here. An overshooting curve would drive the front past the slack margin
    // that the endpoint guarantees below are built on, and a tab swap has no
    // visual meaning outside its endpoints anyway. Same reasoning tab-fade
    // states for its weights.
    float p = clamp(t, 0.0, 1.0);

    // Aspect of the rect uv ACTUALLY spans — the expanded rect (iResolution),
    // not the frame. This is an anchor-extent pack, so uv [0,1] covers frame
    // plus decoration shadow; deriving the correction from the frame left the
    // iris elliptical by the shadow-padding ratio on decorated windows.
    vec2 res = max(iResolution, vec2(1.0));
    float aspect = res.x / res.y;

    // ── Aspect-corrected radial coordinate: rr is 0 at the column's centre
    // and 1 at its corners, so the iris stays circular on any column shape
    // and still finishes at the corners rather than long after them. ──
    vec2 pos = (uv - 0.5) * vec2(aspect, 1.0);
    float maxR = 0.5 * length(vec2(aspect, 1.0));
    float rr = length(pos) / maxR;

    float soft = clamp(p_softness, 0.04, 0.3);
    float turb = clamp(p_turbulence, 0.0, 1.0);
    float arms = clamp(p_arms, 0.0, 9.0);
    float grain = clamp(p_grain, 0.2, 3.0);

    // ── Local iris threshold: the radius roughened by fBm so the rim is eaten
    // by turbulence rather than being a compass circle, plus angular tendrils
    // that push the boundary outward in petals and so reach ahead of the ring
    // into the region that has not changed yet. Both distortions are bounded,
    // which is what lets the slack below guarantee clean endpoints. ──
    // Normalised by the 4-octave fbm ceiling (0.9375) before centring, the
    // same correction phosphor-gate applies to its 3-octave call: centring
    // the raw sum on 0.5 leaves a small permanent negative bias in the
    // wobble, and the two packs should not disagree about a documented
    // concern.
    float n = fbm(pos * grain * 4.0 / max(maxR, 1.0e-4) * 0.5 + 7.3, 4, 2.0) / 0.9375;
    float tendril = 0.0;
    if (arms >= 0.5) {
        float ang = atan(pos.y, pos.x);
        // floor(arms + 0.5) keeps the petal count integral, which is what
        // keeps the tendril field continuous across the atan seam at ±π. A
        // fractional count tears a visible radial slit down the column.
        float armN = floor(arms + 0.5);
        tendril = (0.5 + 0.5 * sin(ang * armN + rr * 5.0 + p * 3.0)) * 0.5;
    }
    // Bounds that everything below depends on: the fbm term lands in
    // ±0.15·turb and the tendril term in [0, 0.06·turb], so wobble is confined
    // to [-0.15·turb, 0.21·turb].
    float wobble = (n - 0.5) * turb * 0.30 + tendril * turb * 0.12;
    float threshold = rr + wobble;

    // How far ahead of the ring the outgoing tab starts draining, and how far
    // behind it the embers survive. Tied to the rim softness rather than given
    // its own parameter, so a softer rim automatically gets a longer approach
    // and the two never separate into a visible gap. Capped at 0.25 so the
    // reach term cannot come to dominate the slack at maximum softness.
    float reach = min(soft * 2.0, 0.25);

    // Expand p past [0, 1] by the gaussian rim margin, the worst-case wobble
    // AND the reach, so every one of the three effects is fully off at both
    // endpoints. The 3x on soft is phosphor-ignite's gaussian margin: it takes
    // the rim glow's exp(-md*md) down to e^-9 rather than merely taking the
    // reveal smoothstep to its end, which the bare soft would do while leaving
    // a visible ring parked on a settled tab.
    //
    // THE COST, stated rather than hidden: front carries the FULL margin, so
    // the reveal itself sits idle while the ring is still off the column —
    // about a fifth of the leg at each end at the defaults, more at maximum
    // softness. That is the deliberate trade for endpoint frames that are
    // bit-clean of glow; splitting the padding (reveal padded by soft only,
    // rim envelope by 3x) would use more of the leg but was rejected because
    // the shipped motion was tuned and approved against this timing.
    float slack = 3.0 * soft + 0.21 * turb + reach;
    float front = p * (1.0 + 2.0 * slack) - slack;

    // m > 0: the iris has passed this pixel and the arriving tab owns it.
    float m = front - threshold;

    // reveal: 0 outside the iris (outgoing tab), 1 inside it (arriving tab).
    float reveal = smoothstep(0.0, soft, m);

    // ── Heat shimmer: the plasma refracts what it is passing over, strongest
    // right at the rim. Squared via multiply because pow(x, 2.0) is undefined
    // for x < 0 per the GLSL spec and m is signed for half of every leg. ──
    float md = m / soft;
    float rim = exp(-md * md);
    vec2 outward = length(pos) > 1.0e-4 ? pos / length(pos) : vec2(0.0);
    vec2 bend = outward * rim * clamp(p_shimmer, 0.0, 1.0) * 0.015 / vec2(aspect, 1.0);
    vec2 suv = uv - bend;

    // The displaced sample is mixed IN by boundaryMask rather than multiplied
    // BY it. Multiplying is what phosphor-ignite does, and it is right there
    // because a not-yet-ignited window has no coverage to lose. Here it would
    // punch the column's own alpha to zero in a thin band at the frame edge
    // for the frames the ring spends near the corners, and a tab swap has an
    // opaque window behind it that would then show wallpaper through that
    // band. Falling back to the undisplaced sample keeps coverage intact and
    // still rejects the clamp-to-edge smear, which is the only thing the mask
    // was ever protecting against.
    // Fetch cost gate: the displaced pair doubles both samplers' fetches, and
    // rim (exp of a squared ratio) is effectively zero everywhere except the
    // thin ring — for the vast majority of fragments suv == uv and the mix is
    // an identity bought at two redundant texture reads. The rim test is
    // coherent across the quad away from the ring, so the branch is nearly
    // free where it matters; with shimmer at zero it removes the doubled
    // fetch over the whole surface for the whole leg.
    vec4 newS;
    vec4 oldS;
    if (rim * clamp(p_shimmer, 0.0, 1.0) < 1.0e-4) {
        newS = surfaceColor(uv);
        oldS = oldColor(uv);
    } else {
        float bm = boundaryMask(suv);
        newS = mix(surfaceColor(uv), surfaceColor(suv), bm);
        oldS = mix(oldColor(uv), oldColor(suv), bm);
    }

    // ── Drain: the outgoing tab sinks toward the family's navy silhouette as
    // the ring closes on it, so the swap reads as the old picture losing its
    // light rather than simply being covered up. approach is 1 at the rim and
    // falls to 0 one reach ahead of it. Behind the rim the -m/reach ratio
    // clamps to 0 and approach stays at 1, which is what makes the last sliver
    // of old content fully drained by the time reveal mixes it out. ──
    float approach = 1.0 - clamp(-m / max(reach, 1.0e-3), 0.0, 1.0);
    float dimK = clamp(p_unlitDim, 0.0, 1.0);
    vec3 tint = length(p_colorTint.rgb) > 0.01 ? p_colorTint.rgb : vec3(0.043, 0.090, 0.188);
    // The family silhouette, verbatim from phosphor-gate: navy shaped by the
    // content's own luminance so the window's layout still reads through it.
    // lum is taken on premultiplied rgb, matching gate, and sil is scaled back
    // by the sample's alpha so the result stays premultiplied.
    float lum = dot(oldS.rgb, vec3(0.299, 0.587, 0.114));
    vec3 sil = tint * (0.5 + 1.6 * lum);
    vec3 drained = mix(oldS.rgb, sil * oldS.a, approach * dimK);

    // Both sides are opaque premultiplied window content and reveal runs the
    // full 0..1, so this mix preserves coverage exactly. This is the line that
    // makes the no-wallpaper-gap property structural rather than something a
    // fill has to patch up afterwards.
    vec4 col = vec4(mix(drained, newS.rgb, reveal), mix(oldS.a, newS.a, reveal));

    // ── Plasma rim: the gradient rides the radius, cyan at the core through
    // to rose at the corners, lifted where the tendrils reach and grained by
    // the family's per-frame sparkle. Coverage-weighted so the light stays
    // inside the window silhouette instead of spilling into the gaps. ──
    vec3 flux = fluxGradient(clamp(rr * 1.1, 0.0, 1.0));
    // Grain on a grid of two LOGICAL pixels of the drawn quad (iResolution is
    // the rect uv spans; on a 2x display the cell is four device px, accepted
    // for a shimmer). The first version mixed spaces (expanded-rect uv times
    // the frame size), skewing the cell by the shadow ratio.
    float sparkle = 0.85 + 0.30 * niriHash(floor(uv * res / 2.0)
                                           + floor(float(iFrame) * 0.2));
    float lift = 0.7 + 0.6 * tendril;
    col.rgb += flux * rim * lift * col.a * clamp(p_glow, 0.0, 2.0) * sparkle;

    // ── Ember sparks shed in the wake: brief twinkles in the freshly-swapped
    // region behind the ring, dying off over one reach (phosphor-stream's
    // motif and its weighting). Emissive over the body only. An unweighted
    // alpha bump would raise alpha where rgb stays near zero on translucent
    // window interiors and composite dark specks instead of light, which is
    // the failure stream's comment calls out. ──
    float sparks = clamp(p_sparks, 0.0, 1.0);
    if (sparks > 0.001) {
        // Mid-leg envelope: exactly zero at both endpoints, so neither the
        // settled arriving tab nor the untouched departing one carries live
        // sparks even before the slack margin is taken into account.
        float mid = clamp(p * (1.0 - p) * 4.0, 0.0, 1.0);
        float wake = (1.0 - clamp(m / max(reach, 1.0e-3), 0.0, 1.0)) * step(0.0, m);
        float h = niriHash(floor(uv * res / 3.0) + floor(p * 40.0) * 0.37);
        float spark = step(0.94, h) * wake * mid * sparks;
        col.rgb += flux * spark * 1.4 * col.a;
    }

    // Bound the additive emissive against the ACTUAL coverage, not 1.0: on a
    // rounded corner or a translucent client area the rim and sparks could
    // otherwise leave rgb above alpha, which blends brighter than opaque over
    // whatever is behind the column. No alpha adjustment: unlike the
    // open/close packs, col.a here is already the window's own coverage
    // rather than a fraction of it being revealed, so there is nothing to
    // raise it toward and raising it at all would push an opacity-ruled
    // window back to opaque.
    col.rgb = clamp(col.rgb, vec3(0.0), vec3(max(col.a, 0.0)));
    return col;
}
