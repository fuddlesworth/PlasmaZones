// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Focus-fade surface shader — not decorative chrome but a focus aid: the
// pack processes the WINDOW CONTENT itself, washing it out while the
// surface is unfocused (desaturate toward luminance, darken, optional
// colour wash) and passing the focused window through untouched. Chains
// with any border pack; place it FIRST in the chain so later packs fold
// over the faded content. All math runs on the premultiplied sample, so
// no un-premultiply round trip is needed: luminance of premultiplied RGB
// is premultiplied luminance, and every mix partner below carries the
// same alpha weighting.
//
// Not animated — the wash flips with uSurfaceFocused, which the host
// pushes on every focus change; no per-frame tick is needed.

#include <surface_color.glsl>

vec4 pSurface(vec2 uv) {
    vec4 c = surfaceTexel(uv);

    // Fully-unfocused look, computed unconditionally so the shader can BLEND
    // between it and the untouched content by uSurfaceFocused. The host ramps
    // uSurfaceFocused between 0 and 1 on a focus change, so the wash fades in
    // and out over a short cross-focus transition rather than snapping at a
    // hard threshold.

    // Desaturate toward (premultiplied) luminance.
    float luma = luma709(c.rgb);
    vec3 faded = mix(c.rgb, vec3(luma), clamp(p_desaturate, 0.0, 1.0));

    // Optional wash: map luminance through the tint colour (sepia by
    // default). The 1.15 lift keeps midtones from muddying, clamped by
    // alpha so the result stays valid premultiplied colour.
    float tint = clamp(p_tintStrength, 0.0, 1.0);
    if (tint > 0.001) {
        vec3 washed = min(p_tintColor.rgb * luma * 1.15, vec3(c.a));
        faded = mix(faded, washed, tint);
    }

    // Darken content only — alpha is untouched, so the window's shape and
    // translucency (and anything a blur pack fills behind it) stay put.
    faded *= 1.0 - clamp(p_dim, 0.0, 0.6);

    // Cross-fade: 1 = focused (untouched content), 0 = fully faded.
    vec3 rgb = mix(faded, c.rgb, clamp(uSurfaceFocused, 0.0, 1.0));

    return vec4(rgb, c.a);
}
