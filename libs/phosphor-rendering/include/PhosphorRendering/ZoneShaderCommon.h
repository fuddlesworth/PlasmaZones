// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/BaseUniforms.h>
#include <PhosphorShell/IUniformExtension.h>

#include <QColor>
#include <QRectF>
#include <QVector>
#include <QVector4D>

#include <atomic>
#include <cstddef>
#include <cstring>

namespace PhosphorRendering {

/**
 * @brief Maximum number of zones the zone-aware UBO supports.
 */
constexpr int MaxZones = 64;

// Re-export so consumers that pull in this header get the full set of shader
// constants without an extra include.
using PhosphorShell::kShaderTimeWrap;

/**
 * @brief GPU uniform buffer layout — BaseUniforms + zone extension.
 *
 * The first 672 bytes are PhosphorShell::BaseUniforms (Shadertoy-compatible).
 * The remaining bytes are the zone extension arrays.
 *
 * This matches the GLSL UBO declaration in common.glsl exactly:
 *   BaseUniforms fields (672 bytes) → zone arrays (4096 bytes) → total 4768 bytes.
 */
struct alignas(16) ZoneShaderUniforms
{
    // ── Base region (PhosphorShell::BaseUniforms, 672 bytes) ─────────
    PhosphorShell::BaseUniforms base;

    // ── Zone extension region ────────────────────────────────────────
    float zoneRects[MaxZones][4];
    float zoneFillColors[MaxZones][4];
    float zoneBorderColors[MaxZones][4];
    float zoneParams[MaxZones][4];
};

static_assert(sizeof(ZoneShaderUniforms) <= 8192, "ZoneShaderUniforms exceeds expected size");
static_assert(offsetof(ZoneShaderUniforms, base) == 0, "base must be at offset 0");
static_assert(offsetof(ZoneShaderUniforms, zoneRects) == sizeof(PhosphorShell::BaseUniforms),
              "zoneRects must follow BaseUniforms with no gap");

/**
 * @brief UBO region offsets for partial updates (reduces GPU bandwidth).
 *
 * Extends PhosphorShell::UboRegions with zone-specific regions.
 */
namespace ZoneShaderUboRegions {

// Re-export base regions
using namespace PhosphorShell::UboRegions;

// Scene header: iResolution through end of BaseUniforms (before zone arrays).
// Used when scene data changes but zone data hasn't.
constexpr size_t K_SCENE_HEADER_OFFSET = offsetof(ZoneShaderUniforms, base.iResolution);
constexpr size_t K_SCENE_HEADER_SIZE = sizeof(PhosphorShell::BaseUniforms) - K_SCENE_HEADER_OFFSET;

// Scene data: iResolution through end of zone arrays. Used when zone data changes.
constexpr size_t K_SCENE_DATA_OFFSET = offsetof(ZoneShaderUniforms, base.iResolution);
constexpr size_t K_SCENE_DATA_SIZE = sizeof(ZoneShaderUniforms) - K_SCENE_DATA_OFFSET;

// Zone extension region (zoneRects/zoneFillColors/zoneBorderColors/zoneParams).
constexpr size_t K_ZONE_EXTENSION_OFFSET = sizeof(PhosphorShell::BaseUniforms);
constexpr size_t K_ZONE_EXTENSION_SIZE = sizeof(ZoneShaderUniforms) - sizeof(PhosphorShell::BaseUniforms);

} // namespace ZoneShaderUboRegions

/**
 * @brief Per-zone payload pushed into the UBO each frame.
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
 * @brief Parsed zone rectangle data for shader rendering.
 */
struct ZoneRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    int zoneNumber = 0;
    bool highlighted = false;
    float borderRadius = 8.0f;
    float borderWidth = 2.0f;
};

/**
 * @brief Parsed zone color data for shader rendering.
 */
struct ZoneColor
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

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
 * @brief Thread-safe zone data snapshot for the render thread.
 */
struct ZoneDataSnapshot
{
    QVector<ZoneRect> rects;
    QVector<ZoneColor> fillColors;
    QVector<ZoneColor> borderColors;
    int zoneCount = 0;
    int highlightedCount = 0;
    int version = 0;
};

} // namespace PhosphorRendering
