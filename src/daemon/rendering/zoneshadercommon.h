// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

#include <QColor>
#include <QRectF>
#include <QVector4D>

namespace PlasmaZones {

/**
 * @brief Maximum number of zones supported by the shader
 *
 * Limited by uniform buffer size constraints and practical usage.
 * 64 zones allows for complex layouts while maintaining performance.
 */
constexpr int MaxZones = 64;

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

    // Custom shader parameters (16 float slots in 4 vec4s)
    float customParams[4][4]; // vec4[4]: 64 bytes at offset 112

    // Custom colors (8 color slots)
    float customColors[8][4]; // vec4[8]: 128 bytes at offset 176

    // Zone data arrays (each element is vec4)
    float zoneRects[MaxZones][4];
    float zoneFillColors[MaxZones][4];
    float zoneBorderColors[MaxZones][4];
    float zoneParams[MaxZones][4];
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

// Scene data: iResolution through end (zone counts, iMouse, params, colors, zone arrays)
constexpr size_t K_SCENE_DATA_OFFSET = offsetof(ZoneShaderUniforms, iResolution);
constexpr size_t K_SCENE_DATA_SIZE = sizeof(ZoneShaderUniforms) - K_SCENE_DATA_OFFSET;

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

} // namespace PlasmaZones
