// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Blur pack, main pass: composite the window's own pixels OVER the blurred
// backdrop (buffer 1), clipped to the frame rect with rounded corners. The
// blur shows through wherever the window content is translucent, which is
// how blur-behind reads: the pack's own contentOpacity parameter fades the
// window content so the blur has translucency to fill (SetOpacity rules do
// not reach custom chains under the retired handlesOpacity contract). Where
// the content is opaque the blur is fully hidden and this pass is a
// passthrough.
//
// DAEMON FALLBACK: daemon hosts have no scene behind a surface, so
// uHasBackdrop is 0 there (and backdropTexel() is transparent). The pane
// degrades to a faint premultiplied tint slab at the same corner rounding,
// so previews still communicate the pack's shape.

#include <surface_multipass.glsl>

vec4 pSurface(vec2 uv) {
    // The raw content sample, the device-px fragment, the frame SDF at the
    // corner radius and the AA slab mask — the shared backdrop-slab open.
    // The slab below stays solid, so translucency reveals the blurred
    // backdrop rather than the raw scene.
    SurfaceSlab slab = surfaceSlabOpen(uv, p_cornerRadius * uSurfaceScale);
    // Fade the window content over the pane; the translucency it frees is
    // filled by the blurred backdrop in slabComposite below.
    slab.window *= clamp(p_contentOpacity, 0.0, 1.0);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 frost;
    if (uHasBackdrop >= 0.5) {
        // Blurred backdrop (premultiplied, effectively opaque under the
        // window) mixed toward the tint, as an opaque slab under the window.
        vec4 blurred = texture(iChannel1, uv);
        frost = vec4(mix(blurred.rgb, tint * blurred.a, tintStrength), blurred.a) * slab.mask;
    } else {
        // No scene behind this surface (daemon hosts): a faint tint slab.
        frost = faintTintSlab(tint, tintStrength, slab.mask);
    }

    // Window content over the blurred slab (both premultiplied).
    return slabComposite(slab.window, frost);
}
