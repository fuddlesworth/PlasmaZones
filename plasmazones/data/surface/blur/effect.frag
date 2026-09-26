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
// The blurred scene runs through the shared backdrop grade (brightness,
// contrast, saturation, then vibrancy), then the tint, and the grain goes on
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
    SurfaceSlab slab = surfaceSlabOpen(uv, cornerPx, surfaceBottomRadius(cornerPx, p_roundBottomCorners), p_edgeSoftness);
    // Fade the window content over the pane; the translucency it frees is
    // filled by the blurred backdrop in slabComposite below.
    slab.window *= clamp(p_contentOpacity, 0.0, 1.0);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 frost;
    if (uHasBackdrop >= 0.5) {
        // Blurred backdrop (premultiplied, effectively opaque under the
        // window): grade it, mix toward the tint, then grain it. The grain is the
        // driver-stable integer hash so the pane looks the same on every GPU,
        // and it is applied last so it dithers the tinted result rather than
        // being crushed by the contrast step.
        vec4 blurred = surfaceBlurTexel(uv);
        // THE SHARED GRADE, not a hand-rolled copy. This ran the divide, the
        // adjust, the vibrancy and the re-multiply inline, behind its own
        // `blurred.a > 0.001` guard, and that threshold is the one corrected
        // everywhere else to a single RGBA8 quantum: 1/255 is about 0.0039, so
        // 0.001 sits BELOW the smallest alpha an 8-bit backdrop can carry and the
        // one representable near-zero alpha fell through it and divided the colour
        // by that alpha. Calling the helper is what stops this pack keeping the
        // old threshold, and is the reason the swap is worth making at all.
        vec4 graded = surfaceBackdropGrade(blurred, p_brightness, p_contrast, p_saturation, p_vibrancy,
                                           p_vibrancyDarkness);
        // The tint and grain move into PREMULTIPLIED space, because that is what
        // the helper hands back, and both are alpha-weighted to match: mix toward
        // `tint * a` rather than `tint`, and scale the grain by the same alpha.
        // Same picture as the un-premultiplied order it replaces, since the grade
        // already clamps into [0, 1] before re-multiplying.
        vec3 col = mix(graded.rgb, tint * graded.a, tintStrength);
        col += surfaceGrain(slab.px, p_noiseStrength) * graded.a;
        // Bounded by the alpha rather than by 1.0, which is what premultiplied
        // means: rgb may not exceed a. The grain is an unbounded add, so without
        // this a grained highlight could break that invariant at a low alpha.
        frost = vec4(clamp(col, 0.0, graded.a), graded.a) * slab.mask;
    } else {
        // Nothing bound behind this surface: a faint tint slab.
        frost = faintTintSlab(tint, tintStrength, slab.mask);
    }

    // Window content over the blurred slab (both premultiplied).
    return slabComposite(slab.window, frost);
}
