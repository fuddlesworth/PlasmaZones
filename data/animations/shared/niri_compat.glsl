// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Niri shader compatibility shim. The 28 niri-ported animation shaders
// in `data/animations/*` (sourced from https://github.com/liixini/shaders)
// share a common preamble that lets niri's body code compile verbatim
// under PlasmaZones' Qt-RHI / GLSL 4.50 contract.
//
// Include this file from any niri port AFTER `<animation_uniforms.glsl>`
// to get the canonical shims:
//
//   • `niri_geo_to_tex`   → identity `mat3`. Niri shaders multiply UVs
//     through this matrix (`niri_geo_to_tex * vec3(uv, 1.0)`) to map
//     geometry coords → texture coords. PlasmaZones samples uTexture0
//     at `vTexCoord` directly (geometry == texture coords here), so the
//     matrix collapses to identity and the niri body line stays
//     verbatim.
//
//   • `niri_random_seed_value()` → per-instance pseudo-random in [0, 1).
//     Niri's `niri_random_seed` is a per-window-instance uniform; we
//     hash `iSurfaceScreenPos.xy` (the surface origin in logical-screen
//     pixels — stable across the leg, varying between surfaces) to
//     produce the same effect: different windows get different patterns
//     while the same window keeps its pattern across a leg.
//
// Both shims are inert when unreferenced — the SPIR-V compiler dead-
// code-eliminates the function and constant in shaders that don't call
// into the niri_random_seed / niri_geo_to_tex code paths. Including the
// compat shim from every niri port keeps the body translatable verbatim
// regardless of which uniforms the source actually touches.
//
// Dependency: `iSurfaceScreenPos` is declared by
// `<animation_uniforms.glsl>` (UBO branch + KWin default-block branch),
// so this file MUST be included after the uniforms header. The bake
// test (`tests/unit/ui/test_animation_shader_bake.cpp`) registers
// `data/animations/shared/` as the include path, which is what makes
// `#include <niri_compat.glsl>` resolve.

#ifndef PLASMAZONES_NIRI_COMPAT_GLSL
#define PLASMAZONES_NIRI_COMPAT_GLSL

const mat3 niri_geo_to_tex = mat3(1.0);

float niri_random_seed_value() {
    return fract(sin(dot(iSurfaceScreenPos.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

#endif // PLASMAZONES_NIRI_COMPAT_GLSL
