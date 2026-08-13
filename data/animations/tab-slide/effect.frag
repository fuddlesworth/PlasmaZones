// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Tab Slide — the two tabs travel together, the outgoing one leaving by one
// edge as the incoming one arrives from the opposite side.
//
// The desktop sibling (desktop-slide, GL-Transitions "directional") wraps one
// full-screen scene with fract() and reads the wrapped side as the incoming
// desktop. That trick does NOT carry over to a window quad: fract() would wrap
// each TAB onto itself, so a tab sliding half out would show its own right half
// pasted onto its left. Two independently displaced samples under one
// complementary coverage split is the honest form here — literally two cards
// passing.
//
// The direction collapses to a single dominant axis (see the body): the two
// sliders pick horizontal or vertical by whichever is stronger, they do not
// compose into a diagonal.
//
// Geometry and texture coordinates coincide, so both samplers take uv directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl carries uOldWindow / oldColor; noise.glsl carries
// boundaryMask, which is what keeps a displaced sample from smearing its edge
// texel across the rest of the column.
#include <old_content.glsl>
#include <noise.glsl>

// p_dirX / p_dirY (customParams[0].xy) are generated from metadata.json.

vec4 pTransition(vec2 uv, float t) {
    // No snapshot of the outgoing tab (capture failed or its window died):
    // bail to the arriving tab AT THE FRAGMENT'S OWN uv. Without this branch
    // oldColor's built-in fallback substitutes the live content at the
    // DISPLACED coordinate, and the leg draws two sliding copies of the
    // arriving tab — a duplicate-window artifact far louder than the missing
    // cross-fade it stands in for. Every pack that samples old and new at
    // different coordinates needs this; the same-uv packs (fade, wipe,
    // dissolve) get the degeneracy from the substitution itself.
    if (iHasOldWindow == 0) {
        return surfaceColor(uv);
    }

    // Clamped: a slide has no third tab to reveal, so there is nothing to
    // overshoot INTO. An overshooting curve would push both tabs off their own
    // rect and leave the column momentarily empty.
    float p = clamp(t, 0.0, 1.0);

    // ONE dominant axis, never a true diagonal. With two independent
    // rectangular masks a diagonal slide leaves two whole quadrants covered by
    // NEITHER side (transparent wallpaper through half the column at p = 0.5),
    // and the partition-of-unity weights below have no direction-agnostic
    // equivalent that heals it. Collapsing to the larger-magnitude component
    // keeps both sliders meaningful — dirY beats dirX when it is the stronger
    // wish — while making the broken configurations unreachable. The all-zero
    // guard keeps a pack whose sliders are both centred from cutting instead
    // of sliding.
    vec2 s;
    if (abs(p_dirY) > abs(p_dirX)) {
        s = vec2(0.0, sign(p_dirY));
    } else if (abs(p_dirX) > 0.0) {
        s = vec2(sign(p_dirX), 0.0);
    } else {
        s = vec2(1.0, 0.0);
    }

    // The outgoing tab walks out along s; the incoming one starts a full rect
    // back along it and lands at 0. Sampling at uv MINUS the displacement is
    // what moves the image WITH the direction.
    vec2 outUv = uv - p * s;
    vec2 inUv = uv + (1.0 - p) * s;

    // A PARTITION OF UNITY by construction: one coverage scalar, and the
    // incoming side takes exactly the remainder. The first version gave each
    // side its own boundaryMask and asserted the pair summed to one at the
    // seam; it summed to TWO — boundaryMask is 1.0 everywhere inside [0,1]
    // and feathers only OUTSIDE it, so along the seam both sides sat at full
    // weight and a band about 0.005 uv wide travelled the column at doubled
    // alpha (negative destination term under premultiplied blending).
    // Complementary weights cannot over-cover, whatever the masks do.
    //
    // The [0,1] domain both sides slide across is the EXPANDED rect (frame
    // plus shadow), deliberately: each card carries its own shadow band with
    // it, which is what two passing cards would really do. Cropping at the
    // frame instead would shear the shadows off mid-flight.
    float c = boundaryMask(outUv);
    return oldColor(outUv) * c + surfaceColor(inUv) * (1.0 - c);
}
