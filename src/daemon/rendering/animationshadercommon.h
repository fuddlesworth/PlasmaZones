// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

namespace PlasmaZones {

/**
 * @brief GPU uniform buffer layout for animation transition shaders (std140)
 *
 * Simplified version of ZoneShaderUniforms for overlay show/hide transitions.
 * No zone data, no multipass, no audio — just progress + custom params + content texture.
 *
 * Total size: ~288 bytes (well within the 16KB UBO minimum on all GPUs).
 */
struct alignas(16) AnimationShaderUniforms
{
    // Qt scene graph transform (identity for fullscreen quad)
    float qt_Matrix[16]; // mat4: 64 bytes at offset 0
    float qt_Opacity; // float: 4 bytes at offset 64

    // Animation timing
    float pz_progress; // float: 4 bytes at offset 68 — animation progress [0.0-1.0]
    float pz_duration; // float: 4 bytes at offset 72 — total duration in ms
    float pz_style_param; // float: 4 bytes at offset 76 — style-specific parameter

    // Content resolution
    float iResolution[2]; // vec2: 8 bytes at offset 80 — content size in pixels

    // Animation metadata
    int pz_mode; // int: 4 bytes at offset 88 — effect mode (0=dissolve, 1=pixelate, etc.)
    int pz_direction; // int: 4 bytes at offset 92 — 0=show (in), 1=hide (out)

    // Custom shader parameters (same 32 float slots as zone shaders)
    // Shaders use `>= 0.0` to distinguish set values from the -1.0 unset sentinel.
    float customParams[8][4]; // vec4[8]: 128 bytes at offset 96
};

static_assert(sizeof(AnimationShaderUniforms) <= 1024, "AnimationShaderUniforms exceeds expected size");

/**
 * @brief UBO region offsets for partial updates
 */
namespace AnimationShaderUboRegions {

constexpr size_t K_PROGRESS_OFFSET = offsetof(AnimationShaderUniforms, pz_progress);
constexpr size_t K_PROGRESS_SIZE = sizeof(float); // Only pz_progress for per-frame updates

constexpr size_t K_FULL_OFFSET = 0;
constexpr size_t K_FULL_SIZE = sizeof(AnimationShaderUniforms);

} // namespace AnimationShaderUboRegions

// pz_mode is reserved for future use. Each animation effect is its own
// shader file loaded from the AnimationShaderRegistry — no mode switching.

} // namespace PlasmaZones
