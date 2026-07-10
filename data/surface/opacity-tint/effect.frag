// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opacity and tint. Fades the surface and optionally washes it with a colour.
// Works in premultiplied-alpha space (surfaceTexel is premultiplied): the tint
// is scaled by the source coverage and the opacity scales rgb and alpha
// together, so the output stays a valid premultiplied texel.

vec4 pSurface(vec2 uv) {
    vec4 c = surfaceTexel(uv);

    // The tint colour's own alpha scales the wash alongside the strength param,
    // so a translucent tint colour tints more gently.
    float tint = clamp(p_tintStrength, 0.0, 1.0) * clamp(p_tintColor.a, 0.0, 1.0);
    if (tint > 0.001) {
        // Premultiply the tint by the surface's coverage so it only lands where
        // the surface is opaque and the mix stays premultiplied.
        vec3 washed = p_tintColor.rgb * c.a;
        c.rgb = mix(c.rgb, washed, tint);
    }

    // Dim the whole premultiplied texel.
    c *= clamp(p_opacity, 0.0, 1.0);

    return c;
}
