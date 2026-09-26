// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mosaic pack: the backdrop pixelated into coarse cells instead of
// blurred — privacy glass. SINGLE PASS: unlike the rest of the Blur
// family this pack needs no Kawase buffers at all. It samples the RAW
// backdrop once per fragment at the cell centre via backdropTexel()
// (which keeps a cell centre that lands outside the surface's own slice
// in range on either runtime: the compositor clamps into the captured
// rect, the daemon leans on the sampler's clamp-to-edge). Same slab
// composite as the blur family: the pane shows through wherever the
// window itself is translucent.
//
// Retired handlesOpacity contract: uSurfaceOpacity is a constant 1.0 now
// (SetOpacity is layer-backed and custom chains own their alpha). The pack's
// own contentOpacity parameter fades the window content so the mosaic shows
// on opaque windows; a theme's own translucent pixels reveal it the same way
// with no parameter involved.
// NO-BACKDROP FALLBACK: when the host bound nothing behind the surface
// (uHasBackdrop = 0), the pack renders a still tint slab with the same
// corner rounding.

#include <surface_backdrop.glsl>

vec4 pSurface(vec2 uv) {
    float cornerPx = p_cornerRadius * uSurfaceScale;
    SurfaceSlab slab = surfaceSlabOpen(uv, cornerPx, surfaceBottomRadius(cornerPx, p_roundBottomCorners), p_edgeSoftness);
    // Fade the window content over the pane; the translucency it frees is
    // filled by the mosaic in slabComposite below.
    slab.window *= clamp(p_contentOpacity, 0.0, 1.0);
    vec2 px = slab.px;
    float mask = slab.mask;

    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Quantise the fragment to its cell centre in device px (anchored to
        // the frame corner so the grid doesn't crawl when the window moves a
        // sub-cell amount), then POINT-SAMPLE the raw backdrop there.
        //
        // One texel per cell, not an average of the cell. The grid stays put
        // relative to the window, which is what the anchoring buys, but the
        // scene point each cell reads sweeps across the backdrop as the window
        // moves, so cell colours jump texel to texel during a drag. Averaging
        // would need a mip or a blur chain and this pack is single-pass by
        // design, which is also what makes it the cheapest pack in the family.
        // The 2.0 is NOT the parameter's floor: cellSize declares a minimum of
        // 4. It guards a hand-edited metadata.json only, keeping a zero or
        // negative out of the divide below.
        float cell = max(p_cellSize, 2.0) * max(uSurfaceScale, 0.001);
        vec2 local = px - uSurfaceFrameTopLeft;
        vec2 snapped = (floor(local / cell) + 0.5) * cell;
        vec4 b = backdropTexel(uv + pxToUv(snapped - local));
        vec3 rgb = mix(b.rgb, p_tintColor.rgb * b.a, clamp(p_tintStrength, 0.0, 1.0));
        pane = vec4(rgb, b.a) * mask;
    } else {
        // Original pseudo look with no backdrop: a still tint slab.
        pane = vec4(p_tintColor.rgb, 1.0) * 0.4 * mask;
    }

    return slabComposite(slab.window, pane);
}
