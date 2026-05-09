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
// Niri uniform shims (`niri_tex` → `uTexture0`; `niri_geo_to_tex` →
// identity mat3; `niri_random_seed` → `niri_random_seed_value()`) are
// provided by `<niri_compat.glsl>`. `texture2D` is rewritten to
// `texture` (GLSL 4.50 core) inline.
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

#version 450

#include <animation_uniforms.glsl>
#include <niri_compat.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define strength customParams[0].x

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 coords_geo = vec3(vTexCoord, 1.0);
    vec3 size_geo = vec3(max(iAnchorSize, vec2(1.0)), 1.0);

    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = coords_geo.xy;
    float PI = 3.141592653589793;

    vec3 tc = niri_geo_to_tex * vec3(uv, 1.0);
    vec4 win = texture(uTexture0, tc.st);

    float to_m = p + sin(PI * p) * strength;
    fragColor = vec4(
        win.r * win.a * to_m,
        win.g * win.a * to_m,
        win.b * win.a * to_m,
        win.a * p
    );
}
