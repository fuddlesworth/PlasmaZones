// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Blur pack, main pass: composite the window's own pixels OVER the blurred
// backdrop (buffer 1), clipped to the frame rect with rounded corners. The
// blur shows through wherever the window content is translucent, which is
// how blur-behind reads: pair the pack with an opacity rule so the window
// has translucency for the blur to fill. Where the content is opaque the
// blur is fully hidden and this pass is a passthrough.
//
// DAEMON FALLBACK: daemon hosts have no scene behind a surface, so
// uHasBackdrop is 0 there (and backdropTexel() is transparent). The pane
// degrades to a faint premultiplied tint slab at the same corner rounding,
// so previews still communicate the pack's shape.

#version 450
#include <surface_lib.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // handlesOpacity: apply the window's rule-resolved opacity to the
    // CONTENT sample only (premultiplied multiply). The slab below stays
    // solid, so translucency reveals the blurred backdrop rather than the
    // raw scene; the host's present pass skips its own final modulation.
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    // Slab mask: the frame rect with rounded corners, 1px AA edge.
    vec2 px = surfacePixel(vTexCoord);
    FrameSDF fs = frameSdf(px, p_cornerRadius * uSurfaceScale);
    float mask = frameMask(fs.d);

    vec3 tint = p_tintColor.rgb;
    float tintStrength = clamp(p_tintStrength, 0.0, 1.0);
    vec4 frost;
    if (uHasBackdrop >= 0.5) {
        // Blurred backdrop (premultiplied, effectively opaque under the
        // window) mixed toward the tint, as an opaque slab under the window.
        vec4 blurred = texture(iChannel1, vTexCoord);
        frost = vec4(mix(blurred.rgb, tint * blurred.a, tintStrength), blurred.a) * mask;
    } else {
        // No scene behind this surface (daemon hosts): a faint tint slab.
        frost = vec4(tint, 1.0) * (0.35 * tintStrength) * mask;
    }

    // Window content over the blurred slab (both premultiplied).
    fragColor = slabComposite(window, frost);
}
