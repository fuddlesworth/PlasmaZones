// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>

namespace PhosphorSurfaceShaders {

/// C++ mirror of the SURFACE shader contract's daemon UBO block — the
/// `#else` (non-PLASMAZONES_KWIN) branch of `data/surface/shared/
/// surface_uniforms.glsl`, a std140 `layout(std140, binding = 0) uniform
/// SurfaceUniforms { … }`. The daemon's Qt-RHI surface-shader runtime
/// uploads this struct byte-for-byte to binding 0; the compositor takes the
/// classic default-block branch instead and never touches this layout.
///
/// This is a DIFFERENT, leaner layout than PhosphorShaders::BaseUniforms
/// (overlay/animation, 672 bytes): surface decoration needs only geometry +
/// focus + time + the pack parameter slots, and it binds its samplers
/// differently (uTexture0 at binding 7, iChannel0-3 at bindings 2-5 — none of
/// which are UBO members; only iChannelResolution sizes live here).
///
/// The per-field std140 offset static_asserts below PIN the byte layout the
/// GLSL UBO branch depends on. If a field is reordered/resized here, the
/// corresponding assert fails at compile time and surface_uniforms.glsl's
/// `#else` block (and every surface effect.frag that encodes customParams[N]
/// slot positions via `#define p_<id>` preambles) MUST be updated to match.
///
/// std140 note: the three trailing geometry vec2s (uSurfaceSize /
/// uSurfaceFrameTopLeft / uSurfaceFrameSize) leave an 8-byte hole at offset
/// 104 before customParams (a vec4 array, 16-byte-aligned). `_pad0` makes that
/// hole explicit so customParams lands at offset 112 exactly as the GLSL
/// compiler places it.
struct alignas(16) SurfaceUniforms
{
    // Transform + opacity from the Qt scene graph (same lead as BaseUniforms).
    float qt_Matrix[16]; // mat4: 64 bytes at offset 0
    float qt_Opacity; // float: 4 bytes at offset 64

    // Host-provided surface state.
    float uSurfaceScale; // float: 4 bytes at offset 68 (logical→device px)
    float uSurfaceFocused; // float: 4 bytes at offset 72 (1.0 focused / 0.0 not)
    float iTime; // float: 4 bytes at offset 76 (continuous seconds; animated packs)

    // Surface geometry, device px (see surface_uniforms.glsl for semantics).
    float uSurfaceSize[2]; // vec2: 8 bytes at offset 80
    float uSurfaceFrameTopLeft[2]; // vec2: 8 bytes at offset 88
    float uSurfaceFrameSize[2]; // vec2: 8 bytes at offset 96

    // Explicit std140 alignment hole (104 → 112) before the vec4 array below.
    float _pad0[2]; // 8 bytes at offset 104

    // Pack-declared parameters (float/int/bool → customParams, colours →
    // customColors), addressed by the registry-generated `p_<id>` preambles.
    float customParams[8][4]; // vec4[8]: 128 bytes at offset 112
    float customColors[16][4]; // vec4[16]: 256 bytes at offset 240

    // Multipass: iChannelResolution[i] = buffer-pass output texture size (.xy).
    float iChannelResolution[4][4]; // vec4[4]: 64 bytes at offset 496
}; // total 560 bytes

static_assert(sizeof(SurfaceUniforms) == 560, "SurfaceUniforms must be exactly 560 bytes (surface UBO contract)");

static_assert(offsetof(SurfaceUniforms, qt_Matrix) == 0, "SurfaceUniforms::qt_Matrix must remain at std140 offset 0");
static_assert(offsetof(SurfaceUniforms, qt_Opacity) == 64,
              "SurfaceUniforms::qt_Opacity must remain at std140 offset 64");
static_assert(offsetof(SurfaceUniforms, uSurfaceScale) == 68,
              "SurfaceUniforms::uSurfaceScale must remain at std140 offset 68");
static_assert(offsetof(SurfaceUniforms, uSurfaceFocused) == 72,
              "SurfaceUniforms::uSurfaceFocused must remain at std140 offset 72");
static_assert(offsetof(SurfaceUniforms, iTime) == 76, "SurfaceUniforms::iTime must remain at std140 offset 76");
static_assert(offsetof(SurfaceUniforms, uSurfaceSize) == 80,
              "SurfaceUniforms::uSurfaceSize must remain at std140 offset 80");
static_assert(offsetof(SurfaceUniforms, uSurfaceFrameTopLeft) == 88,
              "SurfaceUniforms::uSurfaceFrameTopLeft must remain at std140 offset 88");
static_assert(offsetof(SurfaceUniforms, uSurfaceFrameSize) == 96,
              "SurfaceUniforms::uSurfaceFrameSize must remain at std140 offset 96");
static_assert(offsetof(SurfaceUniforms, customParams) == 112,
              "SurfaceUniforms::customParams must remain at std140 offset 112");
static_assert(offsetof(SurfaceUniforms, customColors) == 240,
              "SurfaceUniforms::customColors must remain at std140 offset 240");
static_assert(offsetof(SurfaceUniforms, iChannelResolution) == 496,
              "SurfaceUniforms::iChannelResolution must remain at std140 offset 496");

} // namespace PhosphorSurfaceShaders
