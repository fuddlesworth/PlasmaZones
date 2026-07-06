// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mosaic pack: the backdrop pixelated into coarse cells instead of
// blurred — privacy glass. SINGLE PASS: unlike the rest of the Blur
// family this pack needs no Gaussian buffers, it samples the RAW
// backdrop once per fragment at the cell centre via backdropTexel()
// (which clamps into the valid capture rect on its own). Same slab
// composite as the blur family: the pane shows through wherever the
// window itself is translucent.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the mosaic.
// DAEMON FALLBACK: no scene behind daemon surfaces (uHasBackdrop = 0), so
// the pack renders a still tint slab with the same corner rounding.

#version 450
#include <surface_lib.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    FrameSDF fs = frameSdf(px, p_cornerRadius * uSurfaceScale);
    float mask = frameMask(fs.d);

    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Quantise the fragment to its cell centre in device px (anchored to
        // the frame corner so the grid doesn't crawl when the window moves a
        // sub-cell amount), then sample the raw backdrop there.
        float cell = max(p_cellSize, 2.0) * uSurfaceScale;
        vec2 local = px - uSurfaceFrameTopLeft;
        vec2 snapped = (floor(local / cell) + 0.5) * cell;
        vec4 b = backdropTexel(vTexCoord + pxToUv(snapped - local));
        vec3 rgb = mix(b.rgb, p_tintColor.rgb * b.a, clamp(p_tintStrength, 0.0, 1.0));
        pane = vec4(rgb, b.a) * mask;
    } else {
        // Original pseudo look for daemon surfaces: a still tint slab.
        pane = vec4(p_tintColor.rgb, 1.0) * 0.4 * mask;
    }

    fragColor = slabComposite(window, pane);
}
