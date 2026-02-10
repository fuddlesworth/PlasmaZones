// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/tilingalgorithm.h"

namespace PlasmaZones {

/**
 * @brief Binary Space Partition — recursive balanced subdivision
 *
 * Alternates vertical/horizontal splits, balanced count distribution.
 * Equivalent to Krohnkite's BTree layout.
 *
 * Example with masterRatio=0.55, windowCount=5 (BFS traversal order):
 * ┌───────────┬─────────┐
 * │           │         │
 * │     1     │    3    │
 * │           │         │
 * ├───────────┼────┬────┤
 * │           │    │    │
 * │     2     │ 4  │ 5  │
 * │           │    │    │
 * └───────────┴────┴────┘
 *
 * Key: balanced split (count/2 per side), depth 0 uses masterRatio,
 * deeper levels use 50/50. Even depth = vertical, odd = horizontal.
 * Zone 0 is the top-left region, not the full left column.
 */
class PLASMAZONES_EXPORT BSPTilingAlgorithm : public TilingAlgorithm
{
public:
    QString id() const override { return QStringLiteral("bsp"); }
    QString name() const override { return QStringLiteral("BSP"); }
    QString description() const override { return QStringLiteral("Binary Space Partition with balanced recursive subdivision"); }
    QVector<QRectF> generateZones(int windowCount, const TilingParams& params) const override;
};

} // namespace PlasmaZones
