// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

#include <PhosphorShaders/BaseUniforms.h>
#include <PhosphorShaders/IUniformExtension.h>

#include <QColor>
#include <QRectF>
#include <QVector>
#include <QVector4D>

#include <atomic>
#include <cstring>

namespace PlasmaZones {

/**
 * @brief Maximum number of zones supported by the shader
 */
constexpr int MaxZones = 64;

// Re-export for code that uses PlasmaZones::kShaderTimeWrap
using PhosphorShaders::kShaderTimeWrap;

/**
 * @brief GPU uniform buffer layout — BaseUniforms + zone extension.
 *
 * The first 672 bytes are PhosphorShaders::BaseUniforms (Shadertoy-compatible).
 * The remaining bytes are the zone extension arrays.
 *
 * This matches the GLSL UBO declaration in common.glsl exactly:
 *   BaseUniforms fields (672 bytes) → zone arrays (4096 bytes) → total 4768 bytes.
 */
struct alignas(16) ZoneShaderUniforms
{
    // ── Base region (PhosphorShaders::BaseUniforms, 672 bytes) ─────────
    PhosphorShaders::BaseUniforms base;

    // ── PhosphorZones::Zone extension region (PlasmaZones-specific) ─────────────────
    float zoneRects[MaxZones][4];
    float zoneFillColors[MaxZones][4];
    float zoneBorderColors[MaxZones][4];
    float zoneParams[MaxZones][4];
};

static_assert(sizeof(ZoneShaderUniforms) <= 8192, "ZoneShaderUniforms exceeds expected size");
static_assert(offsetof(ZoneShaderUniforms, base) == 0, "base must be at offset 0");
static_assert(offsetof(ZoneShaderUniforms, zoneRects) == sizeof(PhosphorShaders::BaseUniforms),
              "zoneRects must follow BaseUniforms with no gap");

/**
 * @brief UBO region offsets for partial updates (reduces GPU bandwidth)
 *
 * Extends PhosphorShaders::UboRegions with zone-specific regions.
 */
namespace ZoneShaderUboRegions {

// Re-export base regions
using namespace PhosphorShaders::UboRegions;

// Scene header: iResolution through end of BaseUniforms (before zone arrays)
// Used when scene data changes but zone data hasn't.
constexpr size_t K_SCENE_HEADER_OFFSET = offsetof(ZoneShaderUniforms, base.iResolution);
constexpr size_t K_SCENE_HEADER_SIZE = sizeof(PhosphorShaders::BaseUniforms) - K_SCENE_HEADER_OFFSET;

// Scene data: iResolution through end of zone arrays (everything except
// matrix/opacity at the front). Includes iTimeHi (unavoidable in a contiguous
// upload since it sits between iResolution and zone arrays after the UBO reorder).
// Used when zone data changes.
constexpr size_t K_SCENE_DATA_OFFSET = offsetof(ZoneShaderUniforms, base.iResolution);
constexpr size_t K_SCENE_DATA_SIZE = sizeof(ZoneShaderUniforms) - K_SCENE_DATA_OFFSET;

// PhosphorZones::Zone extension region
constexpr size_t K_ZONE_EXTENSION_OFFSET = sizeof(PhosphorShaders::BaseUniforms);
constexpr size_t K_ZONE_EXTENSION_SIZE = sizeof(ZoneShaderUniforms) - sizeof(PhosphorShaders::BaseUniforms);

} // namespace ZoneShaderUboRegions

/**
 * @brief PhosphorZones::Zone data for passing to the shader node
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
 * @brief Parsed zone color data for shader rendering
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
 * @brief Thread-safe zone data snapshot for render thread
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

} // namespace PlasmaZones
