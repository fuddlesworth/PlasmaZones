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
/// uSurfaceFrameTopLeft / uSurfaceFrameSize) end at offset 104. The two floats
/// uHasBackdrop (104) and uSurfaceOpacity (108) fill what would otherwise be an
/// 8-byte hole before customParams (a vec4 array, 16-byte-aligned), which lands
/// at offset 112 exactly as the GLSL compiler places it.
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

    // 1.0 when the host provides a backdrop capture (compositor branch's
    // uBackdrop sampler); ALWAYS 0.0 on the daemon, whose surfaces have no
    // scene behind them. needsBackdrop packs gate their styling on this.
    float uHasBackdrop; // float: 4 bytes at offset 104 (always 0 on the daemon)

    // The window's rule-resolved opacity on the compositor; the daemon has
    // no rule opacity (qt_Opacity carries host opacity) so this pins to 1.
    float uSurfaceOpacity; // float: 4 bytes at offset 108 (always 1 on the daemon)

    // Pack-declared parameters (float/int/bool → customParams, colours →
    // customColors), addressed by the registry-generated `p_<id>` preambles.
    float customParams[8][4]; // vec4[8]: 128 bytes at offset 112
    float customColors[16][4]; // vec4[16]: 256 bytes at offset 240

    // Multipass: iChannelResolution[i] = buffer-pass output texture size (.xy).
    float iChannelResolution[4][4]; // vec4[4]: 64 bytes at offset 496

    // Audio spectrum bar count (0 when audio is disabled). This UBO is the
    // DAEMON's layout: the daemon writes this member for every surface item when
    // CAVA is on. The KWin effect uses no UBO and pushes the same value as a
    // loose `iAudioSpectrumSize` uniform from its own CAVA provider. The
    // uAudioSpectrum sampler (binding 6) lives in surface_audio.glsl, not the
    // UBO; only the size is a UBO member. A pack reads it via surface_audio.glsl
    // (audioBar / getBass).
    int iAudioSpectrumSize; // int: 4 bytes at offset 560
    int _pad_after_audioSpectrum[3]; // std140 pads to the 16-byte-aligned vec4 below

    // Cursor position for hover-reactive packs. .xy in the surface texture's
    // top-down device-px space (negative when off-surface / no hover source),
    // .zw = .xy normalized by uSurfaceSize. The daemon consumer defaults its
    // inherited iMouse to (-1, -1) so an un-hovered surface carries the
    // off-surface sentinel rather than a phantom top-left hover.
    float iMouse[4]; // vec4: 16 bytes at offset 576

    // User texture sizes: iTextureResolution[i].xy = the pixel size of the
    // texture bound at slot i (slot N feeds uTexture<N+1>, bindings 8-10 on
    // the daemon). The node resolves these live, same as the overlay UBO.
    float iTextureResolution[4][4]; // vec4[4]: 64 bytes at offset 592
}; // total 656 bytes

static_assert(sizeof(SurfaceUniforms) == 656, "SurfaceUniforms must be exactly 656 bytes (surface UBO contract)");

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
static_assert(offsetof(SurfaceUniforms, uHasBackdrop) == 104,
              "SurfaceUniforms::uHasBackdrop must remain at std140 offset 104");
static_assert(offsetof(SurfaceUniforms, uSurfaceOpacity) == 108,
              "SurfaceUniforms::uSurfaceOpacity must remain at std140 offset 108");
static_assert(offsetof(SurfaceUniforms, customParams) == 112,
              "SurfaceUniforms::customParams must remain at std140 offset 112");
static_assert(offsetof(SurfaceUniforms, customColors) == 240,
              "SurfaceUniforms::customColors must remain at std140 offset 240");
static_assert(offsetof(SurfaceUniforms, iChannelResolution) == 496,
              "SurfaceUniforms::iChannelResolution must remain at std140 offset 496");
static_assert(offsetof(SurfaceUniforms, iAudioSpectrumSize) == 560,
              "SurfaceUniforms::iAudioSpectrumSize must remain at std140 offset 560");
static_assert(offsetof(SurfaceUniforms, iMouse) == 576, "SurfaceUniforms::iMouse must remain at std140 offset 576");
static_assert(offsetof(SurfaceUniforms, iTextureResolution) == 592,
              "SurfaceUniforms::iTextureResolution must remain at std140 offset 592");

} // namespace PhosphorSurfaceShaders
