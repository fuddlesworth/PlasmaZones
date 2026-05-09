// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Crazy Parametric transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/crazy-parametric). A
// luminance-flat reveal that fades from p=0.2 to p=1.0.
//
// Niri's crazy-parametric ships symmetric close.glsl/open.glsl. PlasmaZones'
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
// Note on the shader name: niri's source computes a `vec2 offset =
// dir * vec2(sin(p*dist*amplitude*xx), sin(p*dist*amplitude*yy)) /
// smoothness;` then SAMPLES AT THE UN-OFFSET `uv` — the parametric
// offset is computed but discarded, so the actual visible behavior is
// just the `smoothstep(0.2, 1.0, p)` luminance reveal. We follow niri
// exactly: the parametric scaffolding would produce sample_uv values
// of ±2-5 UV units at niri's hardcoded constants (amplitude=120,
// smoothness=0.1) — far off-surface, clampToEdge gives chaotic edge-
// bleed, which is presumably why niri/skwd-wall stopped wiring it
// through. The shader name is aspirational; the actual behavior is
// a clean fade-with-dead-tail.

#version 450

#include <animation_uniforms.glsl>
#include <niri_compat.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 coords_geo = vec3(vTexCoord, 1.0);
    vec3 size_geo = vec3(max(iAnchorSize, vec2(1.0)), 1.0);

    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = coords_geo.xy;

    vec3 tc = niri_geo_to_tex * vec3(uv, 1.0);
    vec4 win = texture(uTexture0, tc.st);

    float reveal = smoothstep(0.2, 1.0, p);
    fragColor = win * reveal;
}
