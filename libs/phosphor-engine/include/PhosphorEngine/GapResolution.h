// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <QVariantMap>
#include <optional>

namespace PhosphorEngine {

/// Shared resolution of gap-override layers expressed as PerScreenKeys-shaped
/// QVariantMaps (context-rule gap overrides, per-screen config overrides).
///
/// Both placement engines consume the same map shape: the snap-side geometry
/// helpers (daemon GeometryUtils) and the autotile PerScreenConfigResolver
/// previously each implemented this resolution with byte-identical layer
/// semantics. This header is the single implementation; the `normalize`
/// callable absorbs the one legitimate divergence (autotile clamps every
/// map-sourced value to its gap range, snapping consumes raw values).
namespace GapResolution {

/// Single gap value from an override map, or nullopt when @p map lacks
/// @p key. @p normalize is applied to the map-sourced value.
template<typename Normalize>
std::optional<int> gapFromOverrideMap(const QVariantMap& map, QLatin1String key, Normalize normalize)
{
    const auto it = map.constFind(key);
    if (it == map.constEnd()) {
        return std::nullopt;
    }
    return normalize(it->toInt());
}

/// Resolve the outer-gap portion of an override map as ONE atomic layer.
///
/// Per-side values are honoured only when the map sets UsePerSideOuterGap,
/// and if the layer yields any outer gap it wins wholesale — no per-key
/// blending with lower-precedence layers. Missing sides in a partial
/// per-side map fall back to the map's own uniform OuterGap or, failing
/// that, @p missingSideBase (the caller's next-layer uniform value, passed
/// pre-normalized). Returns nullopt when the map carries no outer-gap info
/// so the caller falls through to the next precedence layer.
template<typename Normalize>
std::optional<::PhosphorLayout::EdgeGaps> outerGapsFromOverrideMap(const QVariantMap& map, int missingSideBase,
                                                                   Normalize normalize)
{
    if (map.isEmpty()) {
        return std::nullopt;
    }
    const auto uniformIt = map.constFind(QString(PerScreenKeys::OuterGap));
    const bool usePerSide = map.value(QString(PerScreenKeys::UsePerSideOuterGap), false).toBool();
    if (usePerSide) {
        const auto topIt = map.constFind(QString(PerScreenKeys::OuterGapTop));
        const auto bottomIt = map.constFind(QString(PerScreenKeys::OuterGapBottom));
        const auto leftIt = map.constFind(QString(PerScreenKeys::OuterGapLeft));
        const auto rightIt = map.constFind(QString(PerScreenKeys::OuterGapRight));
        if (topIt != map.constEnd() || bottomIt != map.constEnd() || leftIt != map.constEnd()
            || rightIt != map.constEnd()) {
            const int base = (uniformIt != map.constEnd()) ? normalize(uniformIt->toInt()) : missingSideBase;
            return ::PhosphorLayout::EdgeGaps{(topIt != map.constEnd()) ? normalize(topIt->toInt()) : base,
                                              (bottomIt != map.constEnd()) ? normalize(bottomIt->toInt()) : base,
                                              (leftIt != map.constEnd()) ? normalize(leftIt->toInt()) : base,
                                              (rightIt != map.constEnd()) ? normalize(rightIt->toInt()) : base};
        }
    }
    if (uniformIt != map.constEnd()) {
        return ::PhosphorLayout::EdgeGaps::uniform(normalize(uniformIt->toInt()));
    }
    return std::nullopt;
}

} // namespace GapResolution
} // namespace PhosphorEngine
