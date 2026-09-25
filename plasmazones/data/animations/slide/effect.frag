// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Slide transition — directional reveal of the rendered surface.
// `direction` selects the axis: 0=left, 1=right, 2=up, 3=down. The
// surface is sampled through uTexture0; we mask it with a moving
// edge so it slides into / out of view from the chosen direction.

// `direction` is metadata-typed `int`; the registry packs ints into
// the same float slot space, so we read it as a float and round at
// the use site. `p_direction` / `p_parallax` resolve to the
// customParams[0] sub-slots (declaration order in metadata.json).

vec4 pTransition(vec2 uv, float t)
{
    // Visibility = how revealed the surface is. `t` is per-leg
    // progress: SurfaceAnimator runs it 0→1 on show and 1→0 on
    // hide, so the reveal mask grows on show ("slide in") and
    // recedes on hide ("slide out") through the same code path.
    float visibility = clamp(t, 0.0, 1.0);

    int dir = int(clamp(p_direction, 0.0, 3.0));
    float coord;
    if (dir == 0) {
        coord = uv.x;            // slide in from the left
    } else if (dir == 1) {
        coord = 1.0 - uv.x;      // slide in from the right
    } else if (dir == 2) {
        coord = uv.y;            // slide in from the top
    } else {
        coord = 1.0 - uv.y;      // slide in from the bottom
    }

    // Reveal mask: pixels where coord < visibility are shown. With
    // `parallax` > 0, sample with a small per-row offset so the
    // surface appears to "slip" as it slides.
    if (coord > visibility) {
        return vec4(0.0);
    }
    vec2 sampleUv = uv;
    if (p_parallax > 0.0) {
        float offset = p_parallax * (visibility - coord);
        if (dir == 0)      sampleUv.x += offset;
        else if (dir == 1) sampleUv.x -= offset;
        else if (dir == 2) sampleUv.y += offset;
        else               sampleUv.y -= offset;
    }
    // Inside-mask: if the parallax offset pushes the sample UV outside
    // [0,1], emit transparent rather than letting the clampToEdge sampler
    // smear the edge texels along the lead axis. Matches doom's and
    // matrix's off-window guard, so all sliding shaders have a clean
    // silhouette boundary instead of a 1-pixel edge-bleed band.
    if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) {
        return vec4(0.0);
    }
    return surfaceColor(sampleUv);
}
