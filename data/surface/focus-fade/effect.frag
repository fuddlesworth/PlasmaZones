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

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 c = surfaceTexel(vTexCoord);

    if (uSurfaceFocused >= 0.5) {
        fragColor = c;
        return;
    }

    // Desaturate toward (premultiplied) luminance.
    float luma = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 rgb = mix(c.rgb, vec3(luma), clamp(p_desaturate, 0.0, 1.0));

    // Optional wash: map luminance through the tint colour (sepia by
    // default). The 1.15 lift keeps midtones from muddying, clamped by
    // alpha so the result stays valid premultiplied colour.
    float tint = clamp(p_tintStrength, 0.0, 1.0);
    if (tint > 0.001) {
        vec3 washed = min(p_tintColor.rgb * luma * 1.15, vec3(c.a));
        rgb = mix(rgb, washed, tint);
    }

    // Darken content only — alpha is untouched, so the window's shape and
    // translucency (and anything a blur pack fills behind it) stay put.
    rgb *= 1.0 - clamp(p_dim, 0.0, 0.6);

    fragColor = vec4(rgb, c.a);
}
