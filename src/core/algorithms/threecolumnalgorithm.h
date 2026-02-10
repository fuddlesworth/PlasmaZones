// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/tilingalgorithm.h"

namespace PlasmaZones {

/**
 * @brief Master in center, stacks on left and right
 *
 * Designed for ultrawide monitors. Equivalent to Krohnkite's Three Column layout.
 *
 * Example with masterRatio=0.5, masterCount=1, windowCount=6:
 * ┌──────┬──────────┬──────┐
 * │  L1  │          │  R1  │
 * ├──────┤  Master  ├──────┤
 * │  L2  │  (50%)   │  R2  │
 * └──────┴──────────┴──────┘
 *
 * Zone order: center (master) first, then right top-to-bottom, then left top-to-bottom.
 * 2 windows: center + right (no left column).
 */
class PLASMAZONES_EXPORT ThreeColumnTilingAlgorithm : public TilingAlgorithm
{
public:
    QString id() const override { return QStringLiteral("three-column"); }
    QString name() const override { return QStringLiteral("Three Column"); }
    QString description() const override { return QStringLiteral("Master in center, stacks on left and right (ultrawide)"); }
    QVector<QRectF> generateZones(int windowCount, const TilingParams& params) const override;
};

} // namespace PlasmaZones
