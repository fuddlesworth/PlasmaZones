// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/tilingalgorithm.h"

namespace PlasmaZones {

/**
 * @brief Classic dwm-style master-stack layout
 *
 * Master area on the left (masterRatio width), stack on the right.
 * Equivalent to Krohnkite's Tile (default) layout.
 *
 * Example with masterRatio=0.55, masterCount=1, windowCount=4:
 * ┌──────────┬────────┐
 * │          │   S1   │
 * │  Master  ├────────┤
 * │  (55%)   │   S2   │
 * │          ├────────┤
 * │          │   S3   │
 * └──────────┴────────┘
 */
class PLASMAZONES_EXPORT MasterStackTilingAlgorithm : public TilingAlgorithm
{
public:
    QString id() const override { return QStringLiteral("master-stack"); }
    QString name() const override { return QStringLiteral("Master-Stack"); }
    QString description() const override { return QStringLiteral("Master area on left, stack on right (dwm-style)"); }
    QVector<QRectF> generateZones(int windowCount, const TilingParams& params) const override;
};

} // namespace PlasmaZones
