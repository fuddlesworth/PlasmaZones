// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/BaseUniforms.h>

#include <QColor>
#include <QRectF>
#include <QVector>
#include <QVector4D>

#include <cstddef>

namespace PhosphorRendering {

/**
 * @brief Maximum number of zones the zone-aware UBO supports.
 */
constexpr int MaxZones = 64;

// Re-export so consumers that pull in this header get the full set of shader
// constants without an extra include.
using PhosphorShaders::kShaderTimeWrap;

/**
 * @brief GPU uniform buffer layout — BaseUniforms + zone extension.
 *
 * The leading PhosphorShaders::BaseUniforms region is Shadertoy-compatible.
 * The remaining bytes are the zone extension arrays.
 *
 * This matches the GLSL UBO declaration in common.glsl exactly:
 *   BaseUniforms fields → zone arrays → total layout. The exact base size
 *   is `sizeof(PhosphorShaders::BaseUniforms)` (currently 672 bytes; pinned
 *   by the static_asserts in BaseUniforms.h — common.glsl tracks this).
 */
struct alignas(16) ZoneShaderUniforms
{
    // ── Base region (PhosphorShaders::BaseUniforms) ────────────────────
    PhosphorShaders::BaseUniforms base;

    // ── Zone extension region ────────────────────────────────────────
    float zoneRects[MaxZones][4];
    float zoneFillColors[MaxZones][4];
    float zoneBorderColors[MaxZones][4];
    float zoneParams[MaxZones][4];

    // Logical-to-device scale for the lengths carried in zoneParams (corner
    // radius, border width). The GLSL counterpart is `uZoneScale` at the tail
    // of the ZoneUniforms block in data/overlays/shared/common.glsl. std140
    // places a float directly after the preceding vec4 array, so no leading
    // pad is needed; the three trailing floats round the block out to the
    // 16-byte boundary std140 requires.
    // Defaulted to identity for the same reason ZoneUniformExtension's
    // constructor seeds it: a value-initialised `ZoneShaderUniforms u = {}`
    // would otherwise carry a zero scale, and a zero scale multiplies every
    // corner radius and border width to nothing, so zones render square and
    // border-less rather than failing visibly. The two structs are asserted
    // binary-identical below, so both need the safe default.
    float zoneScale = 1.0f;
    float _pad_after_zoneScale[3] = {};
};

// Exact, not a bound. Every member offset below is pinned exactly, so a loose
// `<= 8192` would be the one assert that let an accidental growth through.
static_assert(sizeof(ZoneShaderUniforms) == sizeof(PhosphorShaders::BaseUniforms) + 4112,
              "ZoneShaderUniforms must be BaseUniforms plus the 4112-byte zone extension");
static_assert(offsetof(ZoneShaderUniforms, base) == 0, "base must be at offset 0");
static_assert(offsetof(ZoneShaderUniforms, zoneRects) == sizeof(PhosphorShaders::BaseUniforms),
              "zoneRects must follow BaseUniforms with no gap");
// std140 puts a scalar directly after a vec4 array — pin that, because an
// accidental pad here shifts uZoneScale and every overlay pack silently reads
// garbage as its logical-to-device scale (corners round by a random factor).
static_assert(offsetof(ZoneShaderUniforms, zoneScale)
                  == offsetof(ZoneShaderUniforms, zoneParams) + sizeof(float[MaxZones][4]),
              "zoneScale must follow zoneParams with no gap (std140 scalar after vec4 array)");

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
    // 0, matching ZoneData::borderRadius on the same path. An 8 px default here
    // would be exactly the per-pack corner floor the shared zoneSdf() removed:
    // a configured radius of 0 has to mean square corners, not "round by 8".
    float borderRadius = 0.0f;
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
