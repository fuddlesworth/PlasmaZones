// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

#include <QColor>
#include <QRectF>
#include <QVector>
#include <QVector4D>

namespace PlasmaZones {

/**
 * @brief Maximum number of zones supported by the shader
 *
 * Limited by uniform buffer size constraints and practical usage.
 * 64 zones allows for complex layouts while maintaining performance.
 */
constexpr int MaxZones = 64;

// Shader time wrap period: the daemon tracks iTime at double precision but the
// GPU uniform is float32. Without wrapping, float32 ULP grows with magnitude —
// around 2^17 s of uptime, frame-to-frame deltas round to zero and iTime-driven
// animation freezes. We expose iTime as two floats:
//   iTime   = fmod(fullTime, kShaderTimeWrap)           // always in [0, WRAP)
//   iTimeHi = floor(fullTime / kShaderTimeWrap) * WRAP  // integer wrap offset
// Shaders that only need bounded animation ignore iTimeHi and use iTime directly.
// Shaders that need continuous phase across wraps reconstruct via timeSin/timeCos
// helpers in common.glsl.
// Power of two keeps the split exact and float32 precision uniform: at iTime
// near 1024, ULP is ~0.000122 s — ~136 ULPs per 60 fps frame, smooth.
constexpr double kShaderTimeWrap = 1024.0;

/**
 * @brief GPU uniform buffer layout following std140 rules
 *
 * std140 alignment rules:
 * - float/int: 4 bytes, align to 4
 * - vec2: 8 bytes, align to 8
 * - vec3/vec4: 16 bytes, align to 16
 * - mat4: 64 bytes (4 vec4), align to 16
 * - arrays: element size rounded up to vec4 (16 bytes), align to 16
 *
 * Shared by ZoneShaderNodeRhi (RHI backend).
 */
struct alignas(16) ZoneShaderUniforms
{
    // Transform and opacity from Qt scene graph (offset 0)
    float qt_Matrix[16]; // mat4: 64 bytes at offset 0
    float qt_Opacity; // float: 4 bytes at offset 64

    // Shader timing uniforms (Shadertoy-compatible)
    float iTime; // float: 4 bytes at offset 68
    float iTimeDelta; // float: 4 bytes at offset 72
    int iFrame; // int: 4 bytes at offset 76

    // Resolution (vec2 aligned to 8 bytes)
    float iResolution[2]; // vec2: 8 bytes at offset 80

    // Zone counts
    int zoneCount; // int: 4 bytes at offset 88
    int highlightedCount; // int: 4 bytes at offset 92

    // Mouse position uniform
    // iMouse.xy = mouse position in pixels, iMouse.zw = normalized (0-1)
    float iMouse[4]; // vec4: 16 bytes at offset 96-111

    // Date/time: year, month (1-12), day (1-31), seconds since midnight
    float iDate[4]; // vec4: 16 bytes

    // Custom shader parameters (32 float slots in 8 vec4s)
    float customParams[8][4]; // vec4[8]: 128 bytes at offset 128

    // Custom colors (16 color slots)
    float customColors[16][4]; // vec4[16]: 256 bytes at offset 256

    // Zone data arrays (each element is vec4)
    float zoneRects[MaxZones][4];
    float zoneFillColors[MaxZones][4];
    float zoneBorderColors[MaxZones][4];
    float zoneParams[MaxZones][4];

    // Multi-pass: iChannelResolution[i] = size of texture bound to iChannel i (std140: vec2[4], each 16-byte aligned)
    float iChannelResolution[4][4]; // [i][0]=x, [i][1]=y; [i][2],[i][3] padding

    // Audio spectrum (CAVA): number of bars; 0 = disabled. Texture at binding 6.
    int iAudioSpectrumSize;

    // Vestigial: always 1. Both OpenGL and Vulkan need Y-flip when sampling buffer
    // textures. Kept in the UBO for layout stability (removing shifts all subsequent offsets).
    int iFlipBufferY;
    int _pad_after_audioSpectrum[2];

    // User texture resolutions (bindings 7-10): [i][0]=width, [i][1]=height, [i][2..3]=padding
    float iTextureResolution[4][4];

    // Wrap-offset counterpart of iTime — see kShaderTimeWrap above. iTime is the
    // residual in [0, kShaderTimeWrap); iTimeHi is the integer-wrap offset. Sum
    // reconstructs full elapsed time (up to float32 precision). Changes rarely
    // (once per wrap period), so it gets its own partial-upload region instead
    // of riding along with the per-frame time block.
    float iTimeHi;
    float _pad_after_iTimeHi[3]; // std140: round struct size up to vec4 boundary
};

static_assert(sizeof(ZoneShaderUniforms) <= 8192, "ZoneShaderUniforms exceeds expected size");

/**
 * @brief UBO region offsets and sizes for partial updates (reduces GPU bandwidth)
 *
 * Used by ZoneShaderNodeRhi to upload only changed regions instead of the full block.
 * Layout must match ZoneShaderUniforms and std140 rules.
 */
namespace ZoneShaderUboRegions {

// Transform and opacity from Qt scene graph (mat4 + float)
constexpr size_t K_MATRIX_OPACITY_OFFSET = 0;
constexpr size_t K_MATRIX_OPACITY_SIZE = offsetof(ZoneShaderUniforms, iTime); // 68 bytes

// Animation time block (iTime, iTimeDelta, iFrame)
constexpr size_t K_TIME_BLOCK_OFFSET = offsetof(ZoneShaderUniforms, iTime);
constexpr size_t K_TIME_BLOCK_SIZE = sizeof(float) + sizeof(float) + sizeof(int); // 12 bytes

// Scene header: iResolution, zone counts, iMouse, iDate, customParams, customColors
// (everything between time block and zone arrays — avoids re-uploading zone arrays on mouse/resolution change)
constexpr size_t K_SCENE_HEADER_OFFSET = offsetof(ZoneShaderUniforms, iResolution);
constexpr size_t K_SCENE_HEADER_SIZE = offsetof(ZoneShaderUniforms, zoneRects) - K_SCENE_HEADER_OFFSET;

// Scene data: iResolution through end of iTextureResolution (zone counts, iMouse,
// params, colors, zone arrays). Stops BEFORE iTimeHi so the rare iTimeHi upload
// doesn't get bundled into the per-scene-change path.
constexpr size_t K_SCENE_DATA_OFFSET = offsetof(ZoneShaderUniforms, iResolution);
constexpr size_t K_SCENE_DATA_SIZE = offsetof(ZoneShaderUniforms, iTimeHi) - K_SCENE_DATA_OFFSET;

// iTimeHi block: uploaded on its own when the wrap offset advances (once per
// kShaderTimeWrap seconds). sizeof(float) is fine — std140 padding is ignored
// by updateDynamicBuffer as long as we only upload the 4 bytes that matter.
constexpr size_t K_TIME_HI_OFFSET = offsetof(ZoneShaderUniforms, iTimeHi);
constexpr size_t K_TIME_HI_SIZE = sizeof(float);

} // namespace ZoneShaderUboRegions

/**
 * @brief Zone data for passing to the shader node
 */
struct ZoneData
{
    QRectF rect;
    QColor fillColor;
    QColor borderColor;
    float borderRadius = 0.0f;
    float borderWidth = 2.0f;
    bool isHighlighted = false;
    int zoneNumber = 0;
};

/**
 * @brief Parsed zone rectangle data for shader rendering
 *
 * Stores zone geometry normalized to [0,1] coordinates for GPU processing.
 * Safe to copy between threads.
 */
struct ZoneRect
{
    float x = 0.0f; ///< Left edge (0-1)
    float y = 0.0f; ///< Top edge (0-1)
    float width = 0.0f; ///< Width (0-1)
    float height = 0.0f; ///< Height (0-1)
    int zoneNumber = 0; ///< Zone number for display
    bool highlighted = false; ///< Whether this zone is highlighted
    float borderRadius = 8.0f; ///< Corner radius in pixels (for shader)
    float borderWidth = 2.0f; ///< Border width in pixels (for shader)
};

/**
 * @brief Parsed zone color data for shader rendering
 *
 * Stores RGBA colors normalized to [0,1] for GPU processing.
 */
struct ZoneColor
{
    float r = 0.0f; ///< Red component (0-1)
    float g = 0.0f; ///< Green component (0-1)
    float b = 0.0f; ///< Blue component (0-1)
    float a = 1.0f; ///< Alpha component (0-1)

    ZoneColor() = default;
    ZoneColor(float red, float green, float blue, float alpha = 1.0f)
        : r(red)
        , g(green)
        , b(blue)
        , a(alpha)
    {
    }

    static ZoneColor fromQColor(const QColor& color)
    {
        return ZoneColor(static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                         static_cast<float>(color.blueF()), static_cast<float>(color.alphaF()));
    }

    QVector4D toVector4D() const
    {
        return QVector4D(r, g, b, a);
    }
};

/**
 * @brief Thread-safe zone data snapshot for render thread
 *
 * This structure holds a complete copy of zone state that can be
 * safely read by the render thread while the main thread updates.
 */
struct ZoneDataSnapshot
{
    QVector<ZoneRect> rects;
    QVector<ZoneColor> fillColors;
    QVector<ZoneColor> borderColors;
    int zoneCount = 0;
    int highlightedCount = 0;
    int version = 0; ///< Incremented on each update for change detection
};

} // namespace PlasmaZones
