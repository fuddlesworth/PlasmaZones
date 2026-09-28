// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Duotone pack, main pass: the Kawase-blurred backdrop (iChannel6)
// collapsed to luminance and remapped onto a two-colour gradient — the
// concert-poster look. A contrast exponent shapes the split between the
// shadow and highlight colours. Same slab composite as the blur family:
// the pane shows through wherever the window itself is translucent.
//
// BACKDROP STAGES, in order: the blurred sample is collapsed to luma709, the
// pack's own `contrast` shapes that as an EXPONENT, the result indexes the
// two-colour gradient, and a driver-stable grain goes on last so it dithers the
// finished gradient rather than being crushed by the remap.
//
// This pack does NOT call surfaceBackdropGrade, unlike its blur-family siblings,
// and cannot: it declares no brightness, saturation or vibrancy parameters, and its
// `contrast` is the luminance exponent above rather than the grade's contrast. A
// grade before a duotone remap would also be largely wasted, since the remap
// discards everything but luminance.
//
// Retired handlesOpacity contract: uSurfaceOpacity is a constant 1.0 now
// (SetOpacity is layer-backed and custom chains own their alpha). The pack's
// own contentOpacity parameter fades the window content so the duotone
// backdrop shows on opaque windows; a theme's own translucent pixels reveal
// it the same way with no parameter involved.
// NO-BACKDROP FALLBACK: when the host bound nothing behind the surface
// (uHasBackdrop = 0), the pack renders a still shadow-to-highlight gradient
// slab with the same corner rounding.

#include <surface_multipass.glsl>
#include <surface_color.glsl>
#include <surface_noise.glsl>

vec4 pSurface(vec2 uv) {
    float cornerPx = p_cornerRadius * uSurfaceScale;
    SurfaceSlab slab = surfaceSlabOpen(uv, cornerPx, p_roundBottomCorners >= 0.5 ? cornerPx : 0.0, p_edgeSoftness);
    // Fade the window content over the pane; the translucency it frees is
    // filled by the duotone backdrop in slabComposite below.
    slab.window *= clamp(p_contentOpacity, 0.0, 1.0);

    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        vec4 blurred = surfaceBlurTexel(uv);
        // Un-premultiply before taking luminance so a translucent backdrop
        // region doesn't read darker than it is, then re-weight the mapped
        // colour by the capture's own alpha to stay premultiplied.
        float luma = blurred.a > 0.001 ? luma709(blurred.rgb / blurred.a) : 0.0;
        luma = pow(clamp(luma, 0.0, 1.0), max(p_contrast, 0.05));
        vec3 mapped = mix(p_colorA.rgb, p_colorB.rgb, luma);
        // Driver-stable grain over the two-tone map, which hides the banding
        // a smooth luminance ramp otherwise shows between the two colours.
        mapped += surfaceGrain(slab.px, p_noiseStrength);
        pane = vec4(clamp(mapped, 0.0, 1.0) * blurred.a, blurred.a) * slab.mask;
    } else {
        // Original pseudo look with no backdrop: a vertical
        // shadow-to-highlight gradient slab at modest alpha, running bottom
        // to top across the frame (1.0 - frameUv().y, so the shadow colour
        // is at the bottom).
        vec2 fuv = frameUv(slab.px);
        vec3 grad = mix(p_colorA.rgb, p_colorB.rgb, smoothstep(0.0, 1.0, 1.0 - fuv.y));
        pane = vec4(grad, 1.0) * 0.4 * slab.mask;
    }

    return slabComposite(slab.window, pane);
}
