// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Duotone pack, main pass: the Gaussian-blurred backdrop (buffer 1)
// collapsed to luminance and remapped onto a two-colour gradient — the
// concert-poster look. A contrast exponent shapes the split between the
// shadow and highlight colours. Same slab composite as the blur family:
// the pane shows through wherever the window itself is translucent.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the duotone backdrop.
// DAEMON FALLBACK: no scene behind daemon surfaces (uHasBackdrop = 0), so
// the pack renders a still shadow-to-highlight gradient slab with the
// same corner rounding.

#include <surface_backdrop.glsl>
#include <surface_multipass.glsl>
#include <surface_color.glsl>

vec4 pSurface(vec2 uv) {
    SurfaceSlab slab = surfaceSlabOpen(uv, p_cornerRadius * uSurfaceScale);

    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        vec4 blurred = texture(iChannel1, uv);
        // Un-premultiply before taking luminance so a translucent backdrop
        // region doesn't read darker than it is, then re-weight the mapped
        // colour by the capture's own alpha to stay premultiplied.
        float luma = blurred.a > 0.001 ? luma709(blurred.rgb / blurred.a) : 0.0;
        luma = pow(clamp(luma, 0.0, 1.0), max(p_contrast, 0.05));
        vec3 mapped = mix(p_colorA.rgb, p_colorB.rgb, luma);
        pane = vec4(mapped * blurred.a, blurred.a) * slab.mask;
    } else {
        // Original pseudo look for daemon surfaces: a vertical
        // shadow-to-highlight gradient slab at modest alpha.
        vec2 fuv = frameUv(slab.px);
        vec3 grad = mix(p_colorA.rgb, p_colorB.rgb, smoothstep(0.0, 1.0, 1.0 - fuv.y));
        pane = vec4(grad, 1.0) * 0.4 * slab.mask;
    }

    return slabComposite(slab.window, pane);
}
