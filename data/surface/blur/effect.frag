// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Blur pack, main pass: composite the window's own pixels OVER the blurred
// backdrop (the Kawase chain's last pass, iChannel6), clipped to the frame
// rect with rounded corners. The blur shows through wherever the window
// content is translucent, which is how blur-behind reads: the pack's own
// contentOpacity parameter fades the window content so the blur has
// translucency to fill (SetOpacity rules do not reach custom chains under the
// retired handlesOpacity contract). Where the content is opaque the blur is
// fully hidden and this pass is a passthrough.
//
// The blurred scene runs through brightness / contrast / saturation
// (surfaceColorAdjust), then vibrancy, then the tint, and the grain goes on
// LAST so it dithers the tinted result rather than being crushed by the
// contrast step. These are the same controls a stock blur-behind effect
// exposes, so this pack stands on its own as "the blur" without leaning on the
// glass family's lens.
//
// NO-BACKDROP FALLBACK: when the host bound nothing behind the surface,
// uHasBackdrop is 0 (and backdropTexel() is transparent). The pane
// degrades to a faint premultiplied tint slab at the same corner rounding,
// so previews still communicate the pack's shape. The slab carries a
// visibility floor for that reason: tintStrength 0 means no tint, not an
// invisible pane, so the shape still reads at the parameter's minimum.

#include <surface_multipass.glsl>
#include <surface_color.glsl>
#include <surface_noise.glsl>

vec4 pSurface(vec2 uv) {
    // The raw content sample, the device-px fragment, the frame SDF at the
    // corner radius and the AA slab mask — the shared backdrop-slab open.
    // The slab below stays solid, so translucency reveals the blurred
    // backdrop rather than the raw scene.
    float cornerPx = p_cornerRadius * uSurfaceScale;
    SurfaceSlab slab = surfaceSlabOpen(uv, cornerPx, p_roundBottomCorners >= 0.5 ? cornerPx : 0.0, p_edgeSoftness);
    // Fade the window content over the pane; the translucency it frees is
    // filled by the blurred backdrop in slabComposite below.
    slab.window *= clamp(p_contentOpacity, 0.0, 1.0);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 frost;
    if (uHasBackdrop >= 0.5) {
        // Blurred backdrop (premultiplied, effectively opaque under the
        // window): un-premultiply, run the brightness / contrast / saturation
        // knobs, mix toward the tint, then grain it. The grain is the
        // driver-stable integer hash so the pane looks the same on every GPU,
        // and it is applied last so it dithers the tinted result rather than
        // being crushed by the contrast step. Re-premultiplied by the
        // capture's own alpha, as an opaque slab under the window.
        vec4 blurred = surfaceBlurTexel(uv);
        vec3 col = blurred.a > 0.001 ? blurred.rgb / blurred.a : vec3(0.0);
        col = surfaceColorAdjust(col, p_brightness, p_contrast, p_saturation);
        col = surfaceVibrancy(col, p_vibrancy, p_vibrancyDarkness);
        col = mix(col, tint, tintStrength);
        col += surfaceGrain(slab.px, p_noiseStrength);
        frost = vec4(clamp(col, 0.0, 1.0) * blurred.a, blurred.a) * slab.mask;
    } else {
        // Nothing bound behind this surface: a faint tint slab.
        frost = faintTintSlab(tint, tintStrength, slab.mask);
    }

    // Window content over the blurred slab (both premultiplied).
    return slabComposite(slab.window, frost);
}
