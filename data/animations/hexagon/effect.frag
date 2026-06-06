// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Hexagon — Tron-style hex-tile dissolve. The window is rendered
// through a procedural hex grid; in the first half of the leg
// glowing edge lines fade in, in the second half each hex tile
// shrinks toward its centre and disappears. Visually inspired by
// Burn-My-Windows (hexagon.frag, Simon Schneegans), written
// natively against our `iTime`/`uTexture0`.
//
// ## iTime convention
//
// `progress = 1 - clamp(iTime, 0, 1)` — 0 at visible, 1 at
// destroyed. Per-pixel jitter (a simplex-noise offset on
// progress) staggers tile transitions so they don't all flip at
// once. Show plays the close formula in reverse.
//
// ## Two-phase visual
//
// • glowProgress: 0→1 over first half (progress 0..0.5) — glow
//   lines bloom in.
// • tileProgress: 0→1 over second half (progress 0.5..1) — tiles
//   shrink toward their centre.
//
// On show with progress 1→0: tiles grow first (out of nothing),
// then glow lines fade away as the window settles.
//
// ## Compositing
//
// Glow and line layers are composited on top of the (already
// pre-multiplied) window sample. `additiveBlending=true` adds
// them as light emission; false uses an "over" blend that mixes
// toward the line/glow colour. Both glow.a and line.a are
// multiplied by the window's local alpha so the lines don't
// extend past the original window silhouette.

#include <noise.glsl>

// hash22 + simplex2D hosted in shared/noise.glsl.

// Procedural hex grid — same formulation as BMW. Returns:
//   .xy: cell-relative coords (0,0 at centre, ~1 at upper edge)
//   .z:  distance to closest edge (1 at cell centre, 0 at edge)
//   .w:  squared distance to closest cell centre (for glow shaping)
vec4 getHexagons(vec2 p) {
    float edgeLength = sqrt(4.0 / 3.0);
    vec2 scale = vec2(3.0 * edgeLength, 2.0);

    vec2 a    = mod(p, scale) - scale * 0.5;
    vec2 aAbs = abs(a);
    vec2 b    = mod(p + scale * 0.5, scale) - scale * 0.5;
    vec2 bAbs = abs(b);

    float distA = max(aAbs.x / edgeLength + aAbs.y * 0.5, aAbs.y);
    float distB = max(bAbs.x / edgeLength + bAbs.y * 0.5, bAbs.y);

    float dist = 1.0 - min(distA, distB);
    float glow = min(dot(a, a), dot(b, b)) / 1.5;
    vec2 cellCoords = distA < distB ? a : b;

    return vec4(cellCoords, dist, glow);
}

vec4 pTransition(vec2 uv, float t)
{
    float visibility = clamp(iTime, 0.0, 1.0);
    float progress   = 1.0 - visibility;

    // Per-pixel noise jitter so tiles stagger their transitions.
    vec2 seed   = vec2(p_seedX, p_seedY);
    float noise = simplex2D(uv + seed);
    progress    = clamp(mix(noise - 1.0, noise + 1.0, progress), 0.0, 1.0);

    // Two-phase progress: glow in first half, tile shrink in second.
    float glowProgress = smoothstep(0.0, 1.0, clamp(progress / 0.5, 0.0, 1.0));
    float tileProgress = smoothstep(0.0, 1.0, clamp((progress - 0.5) / 0.5, 0.0, 1.0));

    // Hex sample. texScale ties hex cell size to surface pixels so
    // tiles stay roughly constant-sized regardless of window
    // dimensions; tileScale is the user-facing tuning knob.
    // Floor iResolution so an early-frame surface size of 0 doesn't
    // produce a zero-component texScale (which would divide-by-zero in
    // the lookupOffset below).
    vec2 texScale = 0.1 * max(iResolution, vec2(1.0)) / max(p_tileScale, 0.05);
    vec4 hex      = getHexagons(uv * texScale);

    vec4 oColor = vec4(0.0);

    // Crop outer parts of shrinking tiles. tileProgress < hex.z
    // means we're inside the un-shrunk core of this cell.
    if (tileProgress < hex.z) {
        // Lookup offset toward the cell edge — sampling further from
        // the cell centre as tileProgress grows. The (1-tileProgress)
        // divisor makes the offset diverge as the tile shrinks to
        // nothing, so the lookup eventually goes off the texture.
        // boundaryMask (see noise.glsl) crops the off-texture lookup
        // to transparent so the shrinking tile fades cleanly into the
        // background instead of carrying smeared edge texels outward
        // through the clamp-to-edge sampler.
        vec2 lookupOffset = tileProgress * hex.xy / texScale / max(1.0 - tileProgress, 0.001);
        vec2 lookupCoords = uv + lookupOffset;
        oColor = surfaceColor(lookupCoords) * boundaryMask(lookupCoords);

        vec4 glow = p_glowColor;
        vec4 line = p_lineColor;

        // Glow shaping: stack of pow-curves on hex.w (squared
        // distance to cell centre) gives a tight bright core with
        // soft falloff. The constants come from BMW; tweaking them
        // changes the glow distribution. Clamp to [0,1] so the
        // unbounded shaping (peaks at ~15.5) doesn't blow out the
        // additive composite below into a saturated white bloom.
        glow.a *= pow(hex.w, 20.0) * 10.0 + pow(hex.w, 10.0) * 5.0 + pow(hex.w, 2.0) * 0.5;
        glow.a = clamp(glow.a, 0.0, 1.0);

        // Line shaping: smoothstep-band along the cell edge for a
        // soft anti-aliased outline. Width scaled by lineWidth. At
        // lineWidth==0 both smoothstep edges collapse to 0 and the
        // function degenerates to a hard step at hex.z==0 — a visible
        // hard line where the user asked for "no line at all". Gate on
        // lineWidth so the no-line case zeros line.a cleanly.
        if (p_lineWidth > 0.001) {
            line.a *= 1.0 - smoothstep(p_lineWidth * 0.02 * 0.5, p_lineWidth * 0.02, hex.z);
        } else {
            line.a = 0.0;
        }

        // Both fade in over the first half of progress.
        glow.a *= glowProgress;
        line.a *= glowProgress;

        // Don't extend the lines past the window silhouette — gate
        // by the local sampled alpha. Scale ONLY the alpha channels;
        // multiplying the full vec4 would also pre-attenuate
        // glow.rgb / line.rgb, and the additive composite below then
        // multiplies them by glow.a / line.a a second time, dimming
        // the emission against partly-transparent texels.
        glow.a *= oColor.a;
        line.a *= oColor.a;

        if (p_additiveBlending > 0.5) {
            // Pre-multiplied additive emission.
            oColor.rgb += glow.rgb * glow.a;
            oColor.rgb += line.rgb * line.a;
        } else {
            // "Over" blend on pre-multiplied input. The standard
            // formula `under * (1 - over.a) + over_premul` requires
            // BOTH operands in pre-multiplied form; glow.rgb / line.rgb
            // are straight, so multiply by their alpha at the call site
            // to lift them into pre-multiplied space before the merge.
            oColor.rgb = oColor.rgb * (1.0 - glow.a) + glow.rgb * glow.a;
            oColor.rgb = oColor.rgb * (1.0 - line.a) + line.rgb * line.a;
        }
    }

    return oColor;
}
