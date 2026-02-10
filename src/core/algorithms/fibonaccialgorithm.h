// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/tilingalgorithm.h"

namespace PlasmaZones {

/**
 * @brief Spiral subdivision — each new window peels off from remaining space
 *
 * Equivalent to Krohnkite's Spiral layout.
 *
 * Example with masterRatio=0.5, windowCount=5:
 * ┌──────────┬──────────┐
 * │          │          │
 * │    1     │    2     │
 * │          ├────┬─────┤
 * │          │    │  4  │
 * │          │ 3  ├─────┤
 * │          │    │  5  │
 * └──────────┴────┴─────┘
 *
 * Key difference from BSP: always peels 1 window (greedy), not balanced split,
 * creating progressively smaller zones in a spiral pattern.
 */
class PLASMAZONES_EXPORT FibonacciTilingAlgorithm : public TilingAlgorithm
{
public:
    QString id() const override { return QStringLiteral("fibonacci"); }
    QString name() const override { return QStringLiteral("Fibonacci"); }
    QString description() const override { return QStringLiteral("Spiral subdivision with progressively smaller zones"); }
    QVector<QRectF> generateZones(int windowCount, const TilingParams& params) const override;
};

} // namespace PlasmaZones
