// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Tab Flip — the column turns over like a card, the outgoing tab narrowing to
// its edge and the incoming one opening out from it. The most literal reading
// of what a tab swap is: one card in a stack replaced by the next, with the
// two never coexisting.
//
// An orthographic squash rather than a perspective rotation. A true 3D turn
// wants the near edge to grow while the far edge shrinks, and the extra
// foreshortening reads as depth that the flat window quad cannot honour —
// there is no scene here for the card to have depth IN. The squash keeps the
// silhouette exactly on the column's rect at every frame, which matters more
// on a strip where the neighbouring columns are right there to compare against.
//
// Geometry and texture coordinates coincide, so the samplers take uv directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl carries uOldWindow / oldColor; noise.glsl carries
// boundaryMask, which crops the card once it has narrowed past its rect.
#include <old_content.glsl>
#include <noise.glsl>

// p_vertical / p_shade (customParams[0].xy) come from metadata.json.

vec4 pTransition(vec2 uv, float t) {
    // No snapshot of the outgoing tab: bail to the arriving tab at the
    // fragment's own uv rather than flipping a card that shows the arriving
    // content on BOTH faces (oldColor's fallback substitutes at the squashed
    // cardUv, which reads as the new tab turning over onto itself).
    if (iHasOldWindow == 0) {
        return surfaceColor(uv);
    }

    // Clamped: past either end the card would pass through edge-on a second
    // time and turn inside out. A flip is exactly one half-turn.
    float p = clamp(t, 0.0, 1.0);

    // Width of the card as a fraction of the column: 1 at rest, 0 edge-on at
    // the halfway point, 1 again once the other face has opened out.
    float open = abs(1.0 - 2.0 * p);
    // Floored so the divide below stays finite at exactly p = 0.5. At this
    // value the card is a sub-pixel sliver on any real column, so the mask
    // discards all of it — the floor buys numerical safety, not a visible frame.
    float k = max(open, 1.0e-3);

    // Expand about the centre by the inverse of the card's width: a fragment
    // near the middle of the narrowed card reads from the far edge of the
    // source, which is what compresses the image into the remaining strip.
    vec2 axis = (p_vertical > 0.5) ? vec2(1.0, k) : vec2(k, 1.0);
    vec2 cardUv = (uv - vec2(0.5)) / axis + vec2(0.5);

    // Which face is showing. The swap happens exactly at the edge-on frame,
    // where the card has no width to show it in — so the cut is never visible,
    // which is the whole reason a flip can join two unrelated images cleanly.
    vec4 face = (p < 0.5) ? oldColor(cardUv) : surfaceColor(cardUv);

    // A card turning away from the light loses it. Deepest at the edge-on
    // frame and gone at both ends, so the tab settles at its true colours.
    //
    // The shade multiplies the WHOLE premultiplied vec4 — a coverage fade,
    // not a pigment darkening. That is the deliberate family idiom for
    // shading premultiplied content (fold and ripple-snap document the same
    // choice for their valleys: it keeps the rgb <= a invariant intact for
    // free), and here the wallpaper the fade admits is the same wallpaper
    // the narrowing card is geometrically vacating anyway. An rgb-only dim
    // would diverge from those siblings, not fix anything.
    float shade = mix(1.0, 1.0 - clamp(p_shade, 0.0, 1.0), 1.0 - open);

    return face * boundaryMask(cardUv) * shade;
}
