// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>

namespace PhosphorShell {

/// Time wrap period for float32 precision preservation.
/// See docs/phosphorshell-api-design.md for details.
constexpr double kShaderTimeWrap = 1024.0;

/// GPU uniform buffer layout following std140 rules (base region).
///
/// This is the generic Shadertoy-compatible portion of the UBO. Consumers
/// append application-specific data after this block via IUniformExtension.
///
/// The render node allocates sizeof(BaseUniforms) + extension->extensionSize()
/// bytes for the UBO, writes the base region itself, and calls
/// extension->write() for the remainder.
///
/// Fields at offsets 88–91 (_reserved0, _reserved1) are reserved for
/// consumer use within the base block. PlasmaZones uses them for
/// zoneCount and highlightedCount — consumers that don't need zone
/// awareness should leave them as 0.
struct alignas(16) BaseUniforms
{
    // Transform and opacity from Qt scene graph (offset 0)
    float qt_Matrix[16]; // mat4: 64 bytes at offset 0
    float qt_Opacity; // float: 4 bytes at offset 64

    // Shader timing (Shadertoy-compatible)
    float iTime; // float: 4 bytes at offset 68 (wrapped)
    float iTimeDelta; // float: 4 bytes at offset 72
    int iFrame; // int: 4 bytes at offset 76

    // Resolution
    float iResolution[2]; // vec2: 8 bytes at offset 80

    // Consumer-defined (PlasmaZones: zoneCount, highlightedCount)
    int appField0; // offset 88
    int appField1; // offset 92

    // Mouse position
    float iMouse[4]; // vec4: 16 bytes at offset 96

    // Date/time: year, month (1-12), day (1-31), seconds since midnight
    float iDate[4]; // vec4: 16 bytes at offset 112

    // Custom shader parameters (32 float slots in 8 vec4s)
    float customParams[8][4]; // vec4[8]: 128 bytes at offset 128

    // Custom colors (16 color slots)
    float customColors[16][4]; // vec4[16]: 256 bytes at offset 256

    // Multi-pass: iChannelResolution[i] = buffer texture size
    float iChannelResolution[4][4]; // vec4[4]: 64 bytes at offset 512

    // Audio spectrum
    int iAudioSpectrumSize; // offset 576
    int iFlipBufferY; // offset 580 — always 1 for Y-flip
    int _pad_after_audioSpectrum[2]; // offset 584

    // User texture resolutions (bindings 7-10)
    float iTextureResolution[4][4]; // vec4[4]: 64 bytes at offset 592

    // Wrap-offset counterpart of iTime
    float iTimeHi; // offset 656
    float _pad_after_iTimeHi[3]; // std140 struct alignment → total 672 bytes
};

static_assert(sizeof(BaseUniforms) == 672, "BaseUniforms must be exactly 672 bytes");

/// UBO region offsets and sizes for partial updates (reduces GPU bandwidth).
namespace UboRegions {

// Transform and opacity from Qt scene graph (mat4 + float)
constexpr size_t K_MATRIX_OPACITY_OFFSET = 0;
constexpr size_t K_MATRIX_OPACITY_SIZE = offsetof(BaseUniforms, iTime); // 68 bytes

// Animation time block (iTime, iTimeDelta, iFrame)
constexpr size_t K_TIME_BLOCK_OFFSET = offsetof(BaseUniforms, iTime);
constexpr size_t K_TIME_BLOCK_SIZE = sizeof(float) + sizeof(float) + sizeof(int); // 12 bytes

// Scene header: iResolution through end of iTextureResolution
// (everything between time block and iTimeHi — excludes extension zone arrays)
constexpr size_t K_SCENE_HEADER_OFFSET = offsetof(BaseUniforms, iResolution);
constexpr size_t K_SCENE_HEADER_SIZE = offsetof(BaseUniforms, iTimeHi) - K_SCENE_HEADER_OFFSET;

// iTimeHi block: uploaded when the wrap offset advances (rare)
constexpr size_t K_TIME_HI_OFFSET = offsetof(BaseUniforms, iTimeHi);
constexpr size_t K_TIME_HI_SIZE = sizeof(float);

// Total base size (for extension offset calculation)
constexpr size_t K_BASE_SIZE = sizeof(BaseUniforms);

} // namespace UboRegions

} // namespace PhosphorShell
