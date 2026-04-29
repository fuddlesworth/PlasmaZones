// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRect>
#include <QRectF>

#include <algorithm>

namespace PlasmaZones {

/**
 * @brief Compositor-agnostic geometry helper utilities
 *
 * Shared geometry functions used by all compositor plugins.
 */
namespace GeometryHelpers {

/**
 * @brief Convert QRectF to QRect with edge-consistent rounding.
 *
 * Unlike QRectF::toRect() which rounds x, y, width, height independently,
 * this rounds the edges (left, top, right, bottom) and derives width/height
 * from the rounded edges. This ensures adjacent zones sharing an edge always
 * produce exactly the configured gap between them, even when fractional
 * scaling (e.g. 1.2x) produces non-integer zone boundaries.
 */
inline QRect snapToRect(const QRectF& rf)
{
    const int left = qRound(rf.x());
    const int top = qRound(rf.y());
    const int right = qRound(rf.x() + rf.width());
    const int bottom = qRound(rf.y() + rf.height());
    return QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

} // namespace GeometryHelpers
} // namespace PlasmaZones
