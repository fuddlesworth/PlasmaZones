// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Slide-fade transition — directional reveal of the rendered surface
// (sampled through uTexture0) with a soft alpha gradient at the
// moving edge. Same direction semantics as `slide` (0..3 = left /
// right / up / down) but the leading edge softens via fadeWidth.

// `p_direction` / `p_fadeWidth` resolve to the customParams[0]
// sub-slots (declaration order in metadata.json).

vec4 pTransition(vec2 uv, float t)
{
    // `t` is per-leg progress: SurfaceAnimator runs it 0→1 on
    // show and 1→0 on hide, so the reveal-and-fade region grows on
    // show ("slide-fade in") and recedes on hide ("slide-fade out")
    // through the same code path.
    float visibility = clamp(t, 0.0, 1.0);

    int dir = int(clamp(p_direction, 0.0, 3.0));
    float coord;
    if (dir == 0)      coord = uv.x;
    else if (dir == 1) coord = 1.0 - uv.x;
    else if (dir == 2) coord = uv.y;
    else               coord = 1.0 - uv.y;

    float fw = max(p_fadeWidth, 0.001);
    // Scale the smoothstep window to [0, 1+fw] so at visibility=1 the
    // entire surface (coord ∈ [0, 1]) sits BELOW the lower edge of the
    // smoothstep band — alpha = 1 everywhere, fully revealed. The naive
    // `smoothstep(visibility - fw, visibility, coord)` puts the window
    // at [1-fw, 1] for visibility=1, leaving a permanently-faded
    // trailing band at coord ∈ (1-fw, 1] even when the leg has finished.
    float scaledVis = visibility * (1.0 + fw);
    float alpha = smoothstep(scaledVis - fw, scaledVis, coord);
    alpha = 1.0 - alpha; // 1 inside the revealed region, 0 outside

    vec4 sampled = surfaceColor(uv);
    return sampled * alpha;
}
