// SPDX-FileCopyrightText: 2025 arch-disciple (niri-shader-collection)
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Honeycomb wipe — a hexagonal-grid radial mask that reveals/hides the
// surface from the centre outward. Direct port of arch-disciple's niri
// shader (10_Advanced/honeycomb.kdl): same flat-top hex orientation,
// same axial → cube → round → error-correction snap, same per-cell
// flat shading at the wave front. Result matches niri's reference
// screenshot 1:1 instead of approximating it with a different
// rectangular-Voronoi formulation.
//
// Niri-port note: this shader does NOT translate niri's random_seed to
// a hexSize uniform (PlasmaZones uses hexSize as a hex-cell-size param
// distinct from any random seed concept). Where niri parameters have no
// PlasmaZones counterpart, the relevant niri features (e.g. random
// column phase) are removed in this port.
//
// Niri host bindings → PlasmaZones equivalents (mappings only, not
// renames):
//   niri_clamped_progress  →  iTime          (per-leg [0,1] from
//                                             SurfaceAnimator's
//                                             shaderTime AV)
//   soft_edge_width = 0.15 →  softEdge param (also exposed)
//   niri_tex               →  uTexture0      (live FBO of the
//                                             shaderAnchor item, SRB
//                                             binding 7)
//   niri_geo_to_tex        →  identity       (vTexCoord is already in
//                                             tex space)
//   size_geo               →  iAnchorSize    (visible card; used for the
//                                             aspect ratio. The hexSize
//                                             rescale below anchors to
//                                             iSurfaceScreenPos.w
//                                             (screen height) so hex
//                                             pitch stays constant on
//                                             popup vs. maximized
//                                             windows of a given
//                                             display)
//
// Niri's collection ships open + close as separate shaders that differ
// only by `progress` vs `1 - progress`. SurfaceAnimator already runs
// iTime 0→1 on show and 1→0 on hide, so this single shader covers
// both directions.

#define ROOT_THREE 1.73205080757

// metadata.json declaration order → customParams[0] sub-slots.
//   .x = hexSize  — hex cell circumradius as a fraction of the screen
//                   HEIGHT (default 0.15 → 15% of screen height per
//                   hex). The main() rescale (screen.y / anchor.y)
//                   converts this into the aspect-corrected-normalised
//                   unit that getAxialCoords / getHexCenter consume,
//                   so on a given display the hex pitch is the same
//                   whether the surface is a small popup or a
//                   maximized window. Matches niri's reference look
//                   when surface == screen.
//   .y = softEdge — smoothstep-band width at the wave front, in the
//                   aspect-corrected normalised units that hexDist
//                   uses (NOT screen-anchored — the softness knob
//                   describes "how soft is the wave front" as a
//                   fraction of card span, not a hex feature size).
//                   Default 0.15 matches niri's hard-coded value so
//                   the per-cell reveal cadence reads identically.
float roundValue(float v) { return floor(v + 0.5); }

// Snap fractional axial coords to the integer axial coord of the
// nearest flat-top hex cell. Identical algorithm to niri's
// round_to_hex: convert axial (q, r) → cube (x, y, z) with the
// `x + y + z = 0` invariant, round each component independently, then
// fix up whichever component drifted the most so the invariant holds.
//
// Returns axial (q, r) of the snapped cell.
vec2 roundToHex(vec2 axial)
{
    float x = axial.x;
    float z = axial.y;
    float y = -x - z;

    float rx = roundValue(x);
    float ry = roundValue(y);
    float rz = roundValue(z);

    float dx = abs(rx - x);
    float dy = abs(ry - y);
    float dz = abs(rz - z);

    if (dx > dy && dx > dz) {
        rx = -ry - rz;
    } else if (dy > dz) {
        ry = -rx - rz;
    } else {
        rz = -ry - rx;
    }

    return vec2(rx, rz);
}

// Cartesian → axial for a flat-top hex with circumradius `size`.
// Mirrors niri's get_axial_coords exactly — the 2/3 horizontal step
// and the (-1/3, √3/3) vertical mixing are the canonical flat-top
// inverse of the centre-laying transform below.
vec2 getAxialCoords(vec2 p, float size)
{
    float q = p.x * (2.0 / 3.0) / size;
    float r = (-p.x / 3.0 + (ROOT_THREE / 3.0) * p.y) / size;
    return vec2(q, r);
}

// Axial → Cartesian for a flat-top hex with circumradius `size`.
// Centres lay on a (1.5·size) horizontal lattice with √3·size
// vertical spacing, staggered by half-row per column (the q*0.5
// term inside the y mix).
vec2 getHexCenter(vec2 axial, float size)
{
    float x = axial.x * 1.5 * size;
    float y = ROOT_THREE * (axial.y + axial.x * 0.5) * size;
    return vec2(x, y);
}

vec4 pTransition(vec2 uv, float t)
{
    float progress = clamp(iTime, 0.0, 1.0);

    // Aspect-correct so cells stay regular on non-square surfaces. Use
    // iAnchorSize (the visible card) rather than iResolution (FBO with
    // glow margin) so the aspect matches what the user actually sees on
    // popup cards with a captured glow margin. The 1.0 floor guards
    // against first-frame `iAnchorSize = (0, 0)` and matches the rest
    // of the suite's defensive pattern (matrix/hexagon/pixelate). A
    // sub-pixel y of 0.001 would explode the aspectRatio to ~1000 and
    // warp the hex cells into thin slivers.
    vec2 flooredAnchor = max(iAnchorSize, vec2(1.0));
    float aspectRatio = flooredAnchor.x / flooredAnchor.y;

    vec2 normalizedCoords = vec2(vTexCoord.x * aspectRatio, vTexCoord.y);
    vec2 normalizedCenter = vec2(0.5 * aspectRatio, 0.5);

    // Floor matches metadata.json `min: 0.04` so a host that bypasses
    // metadata validation can't drive the cells below the advertised
    // range. The screenHeight / iAnchorSize.y multiply converts hexSize
    // from "fraction of screen height" into the aspect-corrected-
    // normalised circumradius that getAxialCoords / getHexCenter
    // consume — so hex pixel-size stays constant across popup vs.
    // maximized windows of a given display (on full-screen the
    // multiplier collapses to 1.0 and the math matches niri's
    // reference). Floor guards against the pre-first-frame
    // iSurfaceScreenPos = (0,0) state.
    float screenHeight = max(iSurfaceScreenPos.w, 1.0);
    float unitSize = max(p_hexSize, 0.04) * screenHeight / flooredAnchor.y;
    float softEdgeWidth = max(p_softEdge, 0.001);

    // Snap the fragment to the centre of its enclosing hex cell, then
    // compute the centre's distance from screen centre. Every fragment
    // in the same cell shares one hexDist value — that's the per-cell
    // flat shading that produces visible cellular boundaries at the
    // wave front.
    vec2 axial = getAxialCoords(normalizedCoords, unitSize);
    vec2 snappedAxial = roundToHex(axial);
    vec2 hexCenter = getHexCenter(snappedAxial, unitSize);
    float hexDist = distance(hexCenter, normalizedCenter);

    // Wave front grows linearly with iTime. Niri's
    // `length(centre) * 1.25` heuristic gives the wave room to reach
    // corner cells whose centres sit slightly outside `length(centre)`.
    // Wave starts at `softEdge` BELOW zero so even the centre cell
    // (hexDist ≈ 0) is masked at iTime=0; without that offset, niri's
    // `progress * (max + soft)` formulation would expose the centre
    // cell on the very first frame.
    float maxRevealRadius = length(normalizedCenter) * 1.25;
    float waveRadius = progress * (maxRevealRadius + softEdgeWidth);

    // Per-cell "wave arrival" mask: 0 deep inside, 1 outside,
    // smooth transition at the wave front. Each hex shares one
    // hexDist value (per-cell flat shading), so adjacent cells get
    // different mask values within the smoothstep band — that's the
    // visible cellular boundary at the wave front.
    float mask = smoothstep(waveRadius - softEdgeWidth, waveRadius, hexDist);

    // Quantize the mask to discrete opacity steps. Niri's reference
    // doesn't fade each hex through a continuous gradient — hexes
    // increment through a small number of visible levels (the
    // "popping in" cadence the user expects). Combined with per-cell
    // flat shading, the result is hexes that each visibly STEP from
    // transparent → partial → more-partial → solid as the wave
    // sweeps across them, instead of melting smoothly.
    //
    // 4 divisions producing 5 distinct levels (0, 0.25, 0.5, 0.75, 1.0)
    // is enough to read as discrete increments at popup scale without
    // making the transition feel jerky on a 2 s show animation.
    // `floor(x * N + 0.5) / N` is the canonical "round to nearest of
    // N+1 levels" snap.
    const float kOpacityDivisions = 4.0;
    float steppedMask = floor(mask * kOpacityDivisions + 0.5) / kOpacityDivisions;

    // Sample the live anchor FBO and gate it on the radial mask.
    // Premult-alpha invariant: multiplying both colour and alpha by
    // the same scalar keeps the daemon's blend pipeline composing
    // correctly with the parent chain's opacity.
    vec4 sampled = surfaceColor(vTexCoord);
    return sampled * (1.0 - steppedMask);
}
