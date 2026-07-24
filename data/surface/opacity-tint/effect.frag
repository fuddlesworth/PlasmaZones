// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opacity and tint. Fades the surface and optionally washes it with a colour.
// Works in premultiplied-alpha space (surfaceTexel is premultiplied): the tint
// is scaled by the source coverage and the opacity scales rgb and alpha
// together, so the output stays a valid premultiplied texel.

vec4 pSurface(vec2 uv) {
    vec4 c = surfaceTexel(uv);

    // Strength is the SOLE control over how hard the wash lands — the tint
    // colour's own alpha is deliberately ignored. Multiplying the two gave one
    // effect two knobs that silently compounded (a half-alpha colour at half
    // strength landed at a quarter), which is the double-apply this codebase
    // forbids: a composite consumer applies alpha exactly once. Both routes
    // that reach here could hit it — the settings page's tint picker and a
    // rule's SetTintColor, since the rule builder's colour dialog offers an
    // alpha channel for every colour action.
    float tint = clamp(p_tintStrength, 0.0, 1.0);
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
