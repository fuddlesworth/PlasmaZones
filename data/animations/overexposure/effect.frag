// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Overexposure transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/overexposure).
// Brightness-pumped fade — channel intensities lift over a sin-modulated
// envelope while alpha fades.
//
// Niri's overexposure ships symmetric close.glsl/open.glsl. PlasmaZones'
// runtime flips iTime on reverse legs (1→0 on close, 0→1 on open),
// so we use the niri OPEN body verbatim with `niri_clamped_progress`
// translated to `clamp(iTime, 0.0, 1.0)` and the runtime flip
// auto-mirrors the visual on close — no iIsReversed branch needed.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.
//
// Niri-exact port: keeps niri's `win.r * win.a * to_m` formula
// verbatim. The extra `* win.a` was originally suspected of squaring
// alpha (niri assumes non-premultiplied input; our `uTexture0` is
// premultiplied), but a side-by-side trace showed the difference is
// invisible on opaque window content (the dominant case) and only
// affects translucent edges, where dropping the `* win.a` makes the
// port noticeably brighter than niri. Restoring the extra multiply
// reproduces niri's rendering exactly. The visual at peak (`to_m` ≈
// 1.18) clamps to 1.0 in the framebuffer for both implementations,
// so the over-exposure flash is shared with niri.

vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(t, 0.0, 1.0);
    float PI = 3.141592653589793;

    vec4 win = surfaceColor(uv);

    float to_m = p + sin(PI * p) * p_strength;
    return vec4(
        win.r * win.a * to_m,
        win.g * win.a * to_m,
        win.b * win.a * to_m,
        win.a * p
    );
}
